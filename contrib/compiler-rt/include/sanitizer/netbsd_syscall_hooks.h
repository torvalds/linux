//===-- netbsd_syscall_hooks.h --------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of public sanitizer interface.
//
// System call handlers.
//
// Interface methods declared in this header implement pre- and post- syscall
// actions for the active sanitizer.
// Usage:
//   __sanitizer_syscall_pre_getfoo(...args...);
//   long long res = syscall(SYS_getfoo, ...args...);
//   __sanitizer_syscall_post_getfoo(res, ...args...);
//
// DO NOT EDIT! THIS FILE HAS BEEN GENERATED!
//
// Generated with: generate_netbsd_syscalls.awk
// Generated date: 2018-10-30
// Generated from: syscalls.master,v 1.293 2018/07/31 13:00:13 rjs Exp
//
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_NETBSD_SYSCALL_HOOKS_H
#define SANITIZER_NETBSD_SYSCALL_HOOKS_H

#define __sanitizer_syscall_pre_syscall(code, arg0, arg1, arg2, arg3, arg4,    \
                                        arg5, arg6, arg7)                      \
  __sanitizer_syscall_pre_impl_syscall(                                        \
      (long long)(code), (long long)(arg0), (long long)(arg1),                 \
      (long long)(arg2), (long long)(arg3), (long long)(arg4),                 \
      (long long)(arg5), (long long)(arg6), (long long)(arg7))
#define __sanitizer_syscall_post_syscall(res, code, arg0, arg1, arg2, arg3,    \
                                         arg4, arg5, arg6, arg7)               \
  __sanitizer_syscall_post_impl_syscall(                                       \
      res, (long long)(code), (long long)(arg0), (long long)(arg1),            \
      (long long)(arg2), (long long)(arg3), (long long)(arg4),                 \
      (long long)(arg5), (long long)(arg6), (long long)(arg7))
#define __sanitizer_syscall_pre_exit(rval)                                     \
  __sanitizer_syscall_pre_impl_exit((long long)(rval))
#define __sanitizer_syscall_post_exit(res, rval)                               \
  __sanitizer_syscall_post_impl_exit(res, (long long)(rval))
#define __sanitizer_syscall_pre_fork() __sanitizer_syscall_pre_impl_fork()
#define __sanitizer_syscall_post_fork(res)                                     \
  __sanitizer_syscall_post_impl_fork(res)
#define __sanitizer_syscall_pre_read(fd, buf, nbyte)                           \
  __sanitizer_syscall_pre_impl_read((long long)(fd), (long long)(buf),         \
                                    (long long)(nbyte))
#define __sanitizer_syscall_post_read(res, fd, buf, nbyte)                     \
  __sanitizer_syscall_post_impl_read(res, (long long)(fd), (long long)(buf),   \
                                     (long long)(nbyte))
#define __sanitizer_syscall_pre_write(fd, buf, nbyte)                          \
  __sanitizer_syscall_pre_impl_write((long long)(fd), (long long)(buf),        \
                                     (long long)(nbyte))
#define __sanitizer_syscall_post_write(res, fd, buf, nbyte)                    \
  __sanitizer_syscall_post_impl_write(res, (long long)(fd), (long long)(buf),  \
                                      (long long)(nbyte))
#define __sanitizer_syscall_pre_open(path, flags, mode)                        \
  __sanitizer_syscall_pre_impl_open((long long)(path), (long long)(flags),     \
                                    (long long)(mode))
#define __sanitizer_syscall_post_open(res, path, flags, mode)                  \
  __sanitizer_syscall_post_impl_open(res, (long long)(path),                   \
                                     (long long)(flags), (long long)(mode))
#define __sanitizer_syscall_pre_close(fd)                                      \
  __sanitizer_syscall_pre_impl_close((long long)(fd))
#define __sanitizer_syscall_post_close(res, fd)                                \
  __sanitizer_syscall_post_impl_close(res, (long long)(fd))
#define __sanitizer_syscall_pre_compat_50_wait4(pid, status, options, rusage)  \
  __sanitizer_syscall_pre_impl_compat_50_wait4(                                \
      (long long)(pid), (long long)(status), (long long)(options),             \
      (long long)(rusage))
#define __sanitizer_syscall_post_compat_50_wait4(res, pid, status, options,    \
                                                 rusage)                       \
  __sanitizer_syscall_post_impl_compat_50_wait4(                               \
      res, (long long)(pid), (long long)(status), (long long)(options),        \
      (long long)(rusage))
#define __sanitizer_syscall_pre_compat_43_ocreat(path, mode)                   \
  __sanitizer_syscall_pre_impl_compat_43_ocreat((long long)(path),             \
                                                (long long)(mode))
#define __sanitizer_syscall_post_compat_43_ocreat(res, path, mode)             \
  __sanitizer_syscall_post_impl_compat_43_ocreat(res, (long long)(path),       \
                                                 (long long)(mode))
#define __sanitizer_syscall_pre_link(path, link)                               \
  __sanitizer_syscall_pre_impl_link((long long)(path), (long long)(link))
#define __sanitizer_syscall_post_link(res, path, link)                         \
  __sanitizer_syscall_post_impl_link(res, (long long)(path), (long long)(link))
#define __sanitizer_syscall_pre_unlink(path)                                   \
  __sanitizer_syscall_pre_impl_unlink((long long)(path))
#define __sanitizer_syscall_post_unlink(res, path)                             \
  __sanitizer_syscall_post_impl_unlink(res, (long long)(path))
/* syscall 11 has been skipped */
#define __sanitizer_syscall_pre_chdir(path)                                    \
  __sanitizer_syscall_pre_impl_chdir((long long)(path))
#define __sanitizer_syscall_post_chdir(res, path)                              \
  __sanitizer_syscall_post_impl_chdir(res, (long long)(path))
#define __sanitizer_syscall_pre_fchdir(fd)                                     \
  __sanitizer_syscall_pre_impl_fchdir((long long)(fd))
#define __sanitizer_syscall_post_fchdir(res, fd)                               \
  __sanitizer_syscall_post_impl_fchdir(res, (long long)(fd))
#define __sanitizer_syscall_pre_compat_50_mknod(path, mode, dev)               \
  __sanitizer_syscall_pre_impl_compat_50_mknod(                                \
      (long long)(path), (long long)(mode), (long long)(dev))
#define __sanitizer_syscall_post_compat_50_mknod(res, path, mode, dev)         \
  __sanitizer_syscall_post_impl_compat_50_mknod(                               \
      res, (long long)(path), (long long)(mode), (long long)(dev))
#define __sanitizer_syscall_pre_chmod(path, mode)                              \
  __sanitizer_syscall_pre_impl_chmod((long long)(path), (long long)(mode))
#define __sanitizer_syscall_post_chmod(res, path, mode)                        \
  __sanitizer_syscall_post_impl_chmod(res, (long long)(path), (long long)(mode))
#define __sanitizer_syscall_pre_chown(path, uid, gid)                          \
  __sanitizer_syscall_pre_impl_chown((long long)(path), (long long)(uid),      \
                                     (long long)(gid))
#define __sanitizer_syscall_post_chown(res, path, uid, gid)                    \
  __sanitizer_syscall_post_impl_chown(res, (long long)(path),                  \
                                      (long long)(uid), (long long)(gid))
#define __sanitizer_syscall_pre_break(nsize)                                   \
  __sanitizer_syscall_pre_impl_break((long long)(nsize))
#define __sanitizer_syscall_post_break(res, nsize)                             \
  __sanitizer_syscall_post_impl_break(res, (long long)(nsize))
#define __sanitizer_syscall_pre_compat_20_getfsstat(buf, bufsize, flags)       \
  __sanitizer_syscall_pre_impl_compat_20_getfsstat(                            \
      (long long)(buf), (long long)(bufsize), (long long)(flags))
#define __sanitizer_syscall_post_compat_20_getfsstat(res, buf, bufsize, flags) \
  __sanitizer_syscall_post_impl_compat_20_getfsstat(                           \
      res, (long long)(buf), (long long)(bufsize), (long long)(flags))
#define __sanitizer_syscall_pre_compat_43_olseek(fd, offset, whence)           \
  __sanitizer_syscall_pre_impl_compat_43_olseek(                               \
      (long long)(fd), (long long)(offset), (long long)(whence))
#define __sanitizer_syscall_post_compat_43_olseek(res, fd, offset, whence)     \
  __sanitizer_syscall_post_impl_compat_43_olseek(                              \
      res, (long long)(fd), (long long)(offset), (long long)(whence))
#define __sanitizer_syscall_pre_getpid() __sanitizer_syscall_pre_impl_getpid()
#define __sanitizer_syscall_post_getpid(res)                                   \
  __sanitizer_syscall_post_impl_getpid(res)
#define __sanitizer_syscall_pre_compat_40_mount(type, path, flags, data)       \
  __sanitizer_syscall_pre_impl_compat_40_mount(                                \
      (long long)(type), (long long)(path), (long long)(flags),                \
      (long long)(data))
#define __sanitizer_syscall_post_compat_40_mount(res, type, path, flags, data) \
  __sanitizer_syscall_post_impl_compat_40_mount(                               \
      res, (long long)(type), (long long)(path), (long long)(flags),           \
      (long long)(data))
#define __sanitizer_syscall_pre_unmount(path, flags)                           \
  __sanitizer_syscall_pre_impl_unmount((long long)(path), (long long)(flags))
#define __sanitizer_syscall_post_unmount(res, path, flags)                     \
  __sanitizer_syscall_post_impl_unmount(res, (long long)(path),                \
                                        (long long)(flags))
#define __sanitizer_syscall_pre_setuid(uid)                                    \
  __sanitizer_syscall_pre_impl_setuid((long long)(uid))
#define __sanitizer_syscall_post_setuid(res, uid)                              \
  __sanitizer_syscall_post_impl_setuid(res, (long long)(uid))
#define __sanitizer_syscall_pre_getuid() __sanitizer_syscall_pre_impl_getuid()
#define __sanitizer_syscall_post_getuid(res)                                   \
  __sanitizer_syscall_post_impl_getuid(res)
#define __sanitizer_syscall_pre_geteuid() __sanitizer_syscall_pre_impl_geteuid()
#define __sanitizer_syscall_post_geteuid(res)                                  \
  __sanitizer_syscall_post_impl_geteuid(res)
#define __sanitizer_syscall_pre_ptrace(req, pid, addr, data)                   \
  __sanitizer_syscall_pre_impl_ptrace((long long)(req), (long long)(pid),      \
                                      (long long)(addr), (long long)(data))
#define __sanitizer_syscall_post_ptrace(res, req, pid, addr, data)             \
  __sanitizer_syscall_post_impl_ptrace(res, (long long)(req),                  \
                                       (long long)(pid), (long long)(addr),    \
                                       (long long)(data))
#define __sanitizer_syscall_pre_recvmsg(s, msg, flags)                         \
  __sanitizer_syscall_pre_impl_recvmsg((long long)(s), (long long)(msg),       \
                                       (long long)(flags))
#define __sanitizer_syscall_post_recvmsg(res, s, msg, flags)                   \
  __sanitizer_syscall_post_impl_recvmsg(res, (long long)(s), (long long)(msg), \
                                        (long long)(flags))
#define __sanitizer_syscall_pre_sendmsg(s, msg, flags)                         \
  __sanitizer_syscall_pre_impl_sendmsg((long long)(s), (long long)(msg),       \
                                       (long long)(flags))
#define __sanitizer_syscall_post_sendmsg(res, s, msg, flags)                   \
  __sanitizer_syscall_post_impl_sendmsg(res, (long long)(s), (long long)(msg), \
                                        (long long)(flags))
#define __sanitizer_syscall_pre_recvfrom(s, buf, len, flags, from,             \
                                         fromlenaddr)                          \
  __sanitizer_syscall_pre_impl_recvfrom(                                       \
      (long long)(s), (long long)(buf), (long long)(len), (long long)(flags),  \
      (long long)(from), (long long)(fromlenaddr))
#define __sanitizer_syscall_post_recvfrom(res, s, buf, len, flags, from,       \
                                          fromlenaddr)                         \
  __sanitizer_syscall_post_impl_recvfrom(                                      \
      res, (long long)(s), (long long)(buf), (long long)(len),                 \
      (long long)(flags), (long long)(from), (long long)(fromlenaddr))
#define __sanitizer_syscall_pre_accept(s, name, anamelen)                      \
  __sanitizer_syscall_pre_impl_accept((long long)(s), (long long)(name),       \
                                      (long long)(anamelen))
#define __sanitizer_syscall_post_accept(res, s, name, anamelen)                \
  __sanitizer_syscall_post_impl_accept(res, (long long)(s), (long long)(name), \
                                       (long long)(anamelen))
#define __sanitizer_syscall_pre_getpeername(fdes, asa, alen)                   \
  __sanitizer_syscall_pre_impl_getpeername(                                    \
      (long long)(fdes), (long long)(asa), (long long)(alen))
#define __sanitizer_syscall_post_getpeername(res, fdes, asa, alen)             \
  __sanitizer_syscall_post_impl_getpeername(                                   \
      res, (long long)(fdes), (long long)(asa), (long long)(alen))
#define __sanitizer_syscall_pre_getsockname(fdes, asa, alen)                   \
  __sanitizer_syscall_pre_impl_getsockname(                                    \
      (long long)(fdes), (long long)(asa), (long long)(alen))
#define __sanitizer_syscall_post_getsockname(res, fdes, asa, alen)             \
  __sanitizer_syscall_post_impl_getsockname(                                   \
      res, (long long)(fdes), (long long)(asa), (long long)(alen))
#define __sanitizer_syscall_pre_access(path, flags)                            \
  __sanitizer_syscall_pre_impl_access((long long)(path), (long long)(flags))
#define __sanitizer_syscall_post_access(res, path, flags)                      \
  __sanitizer_syscall_post_impl_access(res, (long long)(path),                 \
                                       (long long)(flags))
#define __sanitizer_syscall_pre_chflags(path, flags)                           \
  __sanitizer_syscall_pre_impl_chflags((long long)(path), (long long)(flags))
#define __sanitizer_syscall_post_chflags(res, path, flags)                     \
  __sanitizer_syscall_post_impl_chflags(res, (long long)(path),                \
                                        (long long)(flags))
#define __sanitizer_syscall_pre_fchflags(fd, flags)                            \
  __sanitizer_syscall_pre_impl_fchflags((long long)(fd), (long long)(flags))
#define __sanitizer_syscall_post_fchflags(res, fd, flags)                      \
  __sanitizer_syscall_post_impl_fchflags(res, (long long)(fd),                 \
                                         (long long)(flags))
#define __sanitizer_syscall_pre_sync() __sanitizer_syscall_pre_impl_sync()
#define __sanitizer_syscall_post_sync(res)                                     \
  __sanitizer_syscall_post_impl_sync(res)
#define __sanitizer_syscall_pre_kill(pid, signum)                              \
  __sanitizer_syscall_pre_impl_kill((long long)(pid), (long long)(signum))
#define __sanitizer_syscall_post_kill(res, pid, signum)                        \
  __sanitizer_syscall_post_impl_kill(res, (long long)(pid), (long long)(signum))
#define __sanitizer_syscall_pre_compat_43_stat43(path, ub)                     \
  __sanitizer_syscall_pre_impl_compat_43_stat43((long long)(path),             \
                                                (long long)(ub))
#define __sanitizer_syscall_post_compat_43_stat43(res, path, ub)               \
  __sanitizer_syscall_post_impl_compat_43_stat43(res, (long long)(path),       \
                                                 (long long)(ub))
#define __sanitizer_syscall_pre_getppid() __sanitizer_syscall_pre_impl_getppid()
#define __sanitizer_syscall_post_getppid(res)                                  \
  __sanitizer_syscall_post_impl_getppid(res)
#define __sanitizer_syscall_pre_compat_43_lstat43(path, ub)                    \
  __sanitizer_syscall_pre_impl_compat_43_lstat43((long long)(path),            \
                                                 (long long)(ub))
#define __sanitizer_syscall_post_compat_43_lstat43(res, path, ub)              \
  __sanitizer_syscall_post_impl_compat_43_lstat43(res, (long long)(path),      \
                                                  (long long)(ub))
#define __sanitizer_syscall_pre_dup(fd)                                        \
  __sanitizer_syscall_pre_impl_dup((long long)(fd))
#define __sanitizer_syscall_post_dup(res, fd)                                  \
  __sanitizer_syscall_post_impl_dup(res, (long long)(fd))
#define __sanitizer_syscall_pre_pipe() __sanitizer_syscall_pre_impl_pipe()
#define __sanitizer_syscall_post_pipe(res)                                     \
  __sanitizer_syscall_post_impl_pipe(res)
#define __sanitizer_syscall_pre_getegid() __sanitizer_syscall_pre_impl_getegid()
#define __sanitizer_syscall_post_getegid(res)                                  \
  __sanitizer_syscall_post_impl_getegid(res)
#define __sanitizer_syscall_pre_profil(samples, size, offset, scale)           \
  __sanitizer_syscall_pre_impl_profil((long long)(samples), (long long)(size), \
                                      (long long)(offset), (long long)(scale))
#define __sanitizer_syscall_post_profil(res, samples, size, offset, scale)     \
  __sanitizer_syscall_post_impl_profil(res, (long long)(samples),              \
                                       (long long)(size), (long long)(offset), \
                                       (long long)(scale))
#define __sanitizer_syscall_pre_ktrace(fname, ops, facs, pid)                  \
  __sanitizer_syscall_pre_impl_ktrace((long long)(fname), (long long)(ops),    \
                                      (long long)(facs), (long long)(pid))
#define __sanitizer_syscall_post_ktrace(res, fname, ops, facs, pid)            \
  __sanitizer_syscall_post_impl_ktrace(res, (long long)(fname),                \
                                       (long long)(ops), (long long)(facs),    \
                                       (long long)(pid))
#define __sanitizer_syscall_pre_compat_13_sigaction13(signum, nsa, osa)        \
  __sanitizer_syscall_pre_impl_compat_13_sigaction13(                          \
      (long long)(signum), (long long)(nsa), (long long)(osa))
#define __sanitizer_syscall_post_compat_13_sigaction13(res, signum, nsa, osa)  \
  __sanitizer_syscall_post_impl_compat_13_sigaction13(                         \
      res, (long long)(signum), (long long)(nsa), (long long)(osa))
#define __sanitizer_syscall_pre_getgid() __sanitizer_syscall_pre_impl_getgid()
#define __sanitizer_syscall_post_getgid(res)                                   \
  __sanitizer_syscall_post_impl_getgid(res)
#define __sanitizer_syscall_pre_compat_13_sigprocmask13(how, mask)             \
  __sanitizer_syscall_pre_impl_compat_13_sigprocmask13((long long)(how),       \
                                                       (long long)(mask))
#define __sanitizer_syscall_post_compat_13_sigprocmask13(res, how, mask)       \
  __sanitizer_syscall_post_impl_compat_13_sigprocmask13(res, (long long)(how), \
                                                        (long long)(mask))
#define __sanitizer_syscall_pre___getlogin(namebuf, namelen)                   \
  __sanitizer_syscall_pre_impl___getlogin((long long)(namebuf),                \
                                          (long long)(namelen))
#define __sanitizer_syscall_post___getlogin(res, namebuf, namelen)             \
  __sanitizer_syscall_post_impl___getlogin(res, (long long)(namebuf),          \
                                           (long long)(namelen))
#define __sanitizer_syscall_pre___setlogin(namebuf)                            \
  __sanitizer_syscall_pre_impl___setlogin((long long)(namebuf))
#define __sanitizer_syscall_post___setlogin(res, namebuf)                      \
  __sanitizer_syscall_post_impl___setlogin(res, (long long)(namebuf))
#define __sanitizer_syscall_pre_acct(path)                                     \
  __sanitizer_syscall_pre_impl_acct((long long)(path))
#define __sanitizer_syscall_post_acct(res, path)                               \
  __sanitizer_syscall_post_impl_acct(res, (long long)(path))
#define __sanitizer_syscall_pre_compat_13_sigpending13()                       \
  __sanitizer_syscall_pre_impl_compat_13_sigpending13()
#define __sanitizer_syscall_post_compat_13_sigpending13(res)                   \
  __sanitizer_syscall_post_impl_compat_13_sigpending13(res)
#define __sanitizer_syscall_pre_compat_13_sigaltstack13(nss, oss)              \
  __sanitizer_syscall_pre_impl_compat_13_sigaltstack13((long long)(nss),       \
                                                       (long long)(oss))
#define __sanitizer_syscall_post_compat_13_sigaltstack13(res, nss, oss)        \
  __sanitizer_syscall_post_impl_compat_13_sigaltstack13(res, (long long)(nss), \
                                                        (long long)(oss))
#define __sanitizer_syscall_pre_ioctl(fd, com, data)                           \
  __sanitizer_syscall_pre_impl_ioctl((long long)(fd), (long long)(com),        \
                                     (long long)(data))
#define __sanitizer_syscall_post_ioctl(res, fd, com, data)                     \
  __sanitizer_syscall_post_impl_ioctl(res, (long long)(fd), (long long)(com),  \
                                      (long long)(data))
#define __sanitizer_syscall_pre_compat_12_oreboot(opt)                         \
  __sanitizer_syscall_pre_impl_compat_12_oreboot((long long)(opt))
#define __sanitizer_syscall_post_compat_12_oreboot(res, opt)                   \
  __sanitizer_syscall_post_impl_compat_12_oreboot(res, (long long)(opt))
#define __sanitizer_syscall_pre_revoke(path)                                   \
  __sanitizer_syscall_pre_impl_revoke((long long)(path))
#define __sanitizer_syscall_post_revoke(res, path)                             \
  __sanitizer_syscall_post_impl_revoke(res, (long long)(path))
#define __sanitizer_syscall_pre_symlink(path, link)                            \
  __sanitizer_syscall_pre_impl_symlink((long long)(path), (long long)(link))
#define __sanitizer_syscall_post_symlink(res, path, link)                      \
  __sanitizer_syscall_post_impl_symlink(res, (long long)(path),                \
                                        (long long)(link))
#define __sanitizer_syscall_pre_readlink(path, buf, count)                     \
  __sanitizer_syscall_pre_impl_readlink((long long)(path), (long long)(buf),   \
                                        (long long)(count))
#define __sanitizer_syscall_post_readlink(res, path, buf, count)               \
  __sanitizer_syscall_post_impl_readlink(res, (long long)(path),               \
                                         (long long)(buf), (long long)(count))
#define __sanitizer_syscall_pre_execve(path, argp, envp)                       \
  __sanitizer_syscall_pre_impl_execve((long long)(path), (long long)(argp),    \
                                      (long long)(envp))
#define __sanitizer_syscall_post_execve(res, path, argp, envp)                 \
  __sanitizer_syscall_post_impl_execve(res, (long long)(path),                 \
                                       (long long)(argp), (long long)(envp))
#define __sanitizer_syscall_pre_umask(newmask)                                 \
  __sanitizer_syscall_pre_impl_umask((long long)(newmask))
#define __sanitizer_syscall_post_umask(res, newmask)                           \
  __sanitizer_syscall_post_impl_umask(res, (long long)(newmask))
#define __sanitizer_syscall_pre_chroot(path)                                   \
  __sanitizer_syscall_pre_impl_chroot((long long)(path))
#define __sanitizer_syscall_post_chroot(res, path)                             \
  __sanitizer_syscall_post_impl_chroot(res, (long long)(path))
#define __sanitizer_syscall_pre_compat_43_fstat43(fd, sb)                      \
  __sanitizer_syscall_pre_impl_compat_43_fstat43((long long)(fd),              \
                                                 (long long)(sb))
#define __sanitizer_syscall_post_compat_43_fstat43(res, fd, sb)                \
  __sanitizer_syscall_post_impl_compat_43_fstat43(res, (long long)(fd),        \
                                                  (long long)(sb))
#define __sanitizer_syscall_pre_compat_43_ogetkerninfo(op, where, size, arg)   \
  __sanitizer_syscall_pre_impl_compat_43_ogetkerninfo(                         \
      (long long)(op), (long long)(where), (long long)(size),                  \
      (long long)(arg))
#define __sanitizer_syscall_post_compat_43_ogetkerninfo(res, op, where, size,  \
                                                        arg)                   \
  __sanitizer_syscall_post_impl_compat_43_ogetkerninfo(                        \
      res, (long long)(op), (long long)(where), (long long)(size),             \
      (long long)(arg))
#define __sanitizer_syscall_pre_compat_43_ogetpagesize()                       \
  __sanitizer_syscall_pre_impl_compat_43_ogetpagesize()
#define __sanitizer_syscall_post_compat_43_ogetpagesize(res)                   \
  __sanitizer_syscall_post_impl_compat_43_ogetpagesize(res)
#define __sanitizer_syscall_pre_compat_12_msync(addr, len)                     \
  __sanitizer_syscall_pre_impl_compat_12_msync((long long)(addr),              \
                                               (long long)(len))
#define __sanitizer_syscall_post_compat_12_msync(res, addr, len)               \
  __sanitizer_syscall_post_impl_compat_12_msync(res, (long long)(addr),        \
                                                (long long)(len))
#define __sanitizer_syscall_pre_vfork() __sanitizer_syscall_pre_impl_vfork()
#define __sanitizer_syscall_post_vfork(res)                                    \
  __sanitizer_syscall_post_impl_vfork(res)
/* syscall 67 has been skipped */
/* syscall 68 has been skipped */
/* syscall 69 has been skipped */
/* syscall 70 has been skipped */
#define __sanitizer_syscall_pre_compat_43_ommap(addr, len, prot, flags, fd,    \
                                                pos)                           \
  __sanitizer_syscall_pre_impl_compat_43_ommap(                                \
      (long long)(addr), (long long)(len), (long long)(prot),                  \
      (long long)(flags), (long long)(fd), (long long)(pos))
#define __sanitizer_syscall_post_compat_43_ommap(res, addr, len, prot, flags,  \
                                                 fd, pos)                      \
  __sanitizer_syscall_post_impl_compat_43_ommap(                               \
      res, (long long)(addr), (long long)(len), (long long)(prot),             \
      (long long)(flags), (long long)(fd), (long long)(pos))
#define __sanitizer_syscall_pre_vadvise(anom)                                  \
  __sanitizer_syscall_pre_impl_vadvise((long long)(anom))
#define __sanitizer_syscall_post_vadvise(res, anom)                            \
  __sanitizer_syscall_post_impl_vadvise(res, (long long)(anom))
#define __sanitizer_syscall_pre_munmap(addr, len)                              \
  __sanitizer_syscall_pre_impl_munmap((long long)(addr), (long long)(len))
#define __sanitizer_syscall_post_munmap(res, addr, len)                        \
  __sanitizer_syscall_post_impl_munmap(res, (long long)(addr), (long long)(len))
#define __sanitizer_syscall_pre_mprotect(addr, len, prot)                      \
  __sanitizer_syscall_pre_impl_mprotect((long long)(addr), (long long)(len),   \
                                        (long long)(prot))
#define __sanitizer_syscall_post_mprotect(res, addr, len, prot)                \
  __sanitizer_syscall_post_impl_mprotect(res, (long long)(addr),               \
                                         (long long)(len), (long long)(prot))
#define __sanitizer_syscall_pre_madvise(addr, len, behav)                      \
  __sanitizer_syscall_pre_impl_madvise((long long)(addr), (long long)(len),    \
                                       (long long)(behav))
#define __sanitizer_syscall_post_madvise(res, addr, len, behav)                \
  __sanitizer_syscall_post_impl_madvise(res, (long long)(addr),                \
                                        (long long)(len), (long long)(behav))
/* syscall 76 has been skipped */
/* syscall 77 has been skipped */
#define __sanitizer_syscall_pre_mincore(addr, len, vec)                        \
  __sanitizer_syscall_pre_impl_mincore((long long)(addr), (long long)(len),    \
                                       (long long)(vec))
#define __sanitizer_syscall_post_mincore(res, addr, len, vec)                  \
  __sanitizer_syscall_post_impl_mincore(res, (long long)(addr),                \
                                        (long long)(len), (long long)(vec))
#define __sanitizer_syscall_pre_getgroups(gidsetsize, gidset)                  \
  __sanitizer_syscall_pre_impl_getgroups((long long)(gidsetsize),              \
                                         (long long)(gidset))
#define __sanitizer_syscall_post_getgroups(res, gidsetsize, gidset)            \
  __sanitizer_syscall_post_impl_getgroups(res, (long long)(gidsetsize),        \
                                          (long long)(gidset))
#define __sanitizer_syscall_pre_setgroups(gidsetsize, gidset)                  \
  __sanitizer_syscall_pre_impl_setgroups((long long)(gidsetsize),              \
                                         (long long)(gidset))
#define __sanitizer_syscall_post_setgroups(res, gidsetsize, gidset)            \
  __sanitizer_syscall_post_impl_setgroups(res, (long long)(gidsetsize),        \
                                          (long long)(gidset))
#define __sanitizer_syscall_pre_getpgrp() __sanitizer_syscall_pre_impl_getpgrp()
#define __sanitizer_syscall_post_getpgrp(res)                                  \
  __sanitizer_syscall_post_impl_getpgrp(res)
#define __sanitizer_syscall_pre_setpgid(pid, pgid)                             \
  __sanitizer_syscall_pre_impl_setpgid((long long)(pid), (long long)(pgid))
#define __sanitizer_syscall_post_setpgid(res, pid, pgid)                       \
  __sanitizer_syscall_post_impl_setpgid(res, (long long)(pid),                 \
                                        (long long)(pgid))
#define __sanitizer_syscall_pre_compat_50_setitimer(which, itv, oitv)          \
  __sanitizer_syscall_pre_impl_compat_50_setitimer(                            \
      (long long)(which), (long long)(itv), (long long)(oitv))
#define __sanitizer_syscall_post_compat_50_setitimer(res, which, itv, oitv)    \
  __sanitizer_syscall_post_impl_compat_50_setitimer(                           \
      res, (long long)(which), (long long)(itv), (long long)(oitv))
#define __sanitizer_syscall_pre_compat_43_owait()                              \
  __sanitizer_syscall_pre_impl_compat_43_owait()
#define __sanitizer_syscall_post_compat_43_owait(res)                          \
  __sanitizer_syscall_post_impl_compat_43_owait(res)
#define __sanitizer_syscall_pre_compat_12_oswapon(name)                        \
  __sanitizer_syscall_pre_impl_compat_12_oswapon((long long)(name))
#define __sanitizer_syscall_post_compat_12_oswapon(res, name)                  \
  __sanitizer_syscall_post_impl_compat_12_oswapon(res, (long long)(name))
#define __sanitizer_syscall_pre_compat_50_getitimer(which, itv)                \
  __sanitizer_syscall_pre_impl_compat_50_getitimer((long long)(which),         \
                                                   (long long)(itv))
#define __sanitizer_syscall_post_compat_50_getitimer(res, which, itv)          \
  __sanitizer_syscall_post_impl_compat_50_getitimer(res, (long long)(which),   \
                                                    (long long)(itv))
#define __sanitizer_syscall_pre_compat_43_ogethostname(hostname, len)          \
  __sanitizer_syscall_pre_impl_compat_43_ogethostname((long long)(hostname),   \
                                                      (long long)(len))
#define __sanitizer_syscall_post_compat_43_ogethostname(res, hostname, len)    \
  __sanitizer_syscall_post_impl_compat_43_ogethostname(                        \
      res, (long long)(hostname), (long long)(len))
#define __sanitizer_syscall_pre_compat_43_osethostname(hostname, len)          \
  __sanitizer_syscall_pre_impl_compat_43_osethostname((long long)(hostname),   \
                                                      (long long)(len))
#define __sanitizer_syscall_post_compat_43_osethostname(res, hostname, len)    \
  __sanitizer_syscall_post_impl_compat_43_osethostname(                        \
      res, (long long)(hostname), (long long)(len))
#define __sanitizer_syscall_pre_compat_43_ogetdtablesize()                     \
  __sanitizer_syscall_pre_impl_compat_43_ogetdtablesize()
#define __sanitizer_syscall_post_compat_43_ogetdtablesize(res)                 \
  __sanitizer_syscall_post_impl_compat_43_ogetdtablesize(res)
#define __sanitizer_syscall_pre_dup2(from, to)                                 \
  __sanitizer_syscall_pre_impl_dup2((long long)(from), (long long)(to))
#define __sanitizer_syscall_post_dup2(res, from, to)                           \
  __sanitizer_syscall_post_impl_dup2(res, (long long)(from), (long long)(to))
/* syscall 91 has been skipped */
#define __sanitizer_syscall_pre_fcntl(fd, cmd, arg)                            \
  __sanitizer_syscall_pre_impl_fcntl((long long)(fd), (long long)(cmd),        \
                                     (long long)(arg))
#define __sanitizer_syscall_post_fcntl(res, fd, cmd, arg)                      \
  __sanitizer_syscall_post_impl_fcntl(res, (long long)(fd), (long long)(cmd),  \
                                      (long long)(arg))
#define __sanitizer_syscall_pre_compat_50_select(nd, in, ou, ex, tv)           \
  __sanitizer_syscall_pre_impl_compat_50_select(                               \
      (long long)(nd), (long long)(in), (long long)(ou), (long long)(ex),      \
      (long long)(tv))
#define __sanitizer_syscall_post_compat_50_select(res, nd, in, ou, ex, tv)     \
  __sanitizer_syscall_post_impl_compat_50_select(                              \
      res, (long long)(nd), (long long)(in), (long long)(ou), (long long)(ex), \
      (long long)(tv))
/* syscall 94 has been skipped */
#define __sanitizer_syscall_pre_fsync(fd)                                      \
  __sanitizer_syscall_pre_impl_fsync((long long)(fd))
#define __sanitizer_syscall_post_fsync(res, fd)                                \
  __sanitizer_syscall_post_impl_fsync(res, (long long)(fd))
#define __sanitizer_syscall_pre_setpriority(which, who, prio)                  \
  __sanitizer_syscall_pre_impl_setpriority(                                    \
      (long long)(which), (long long)(who), (long long)(prio))
#define __sanitizer_syscall_post_setpriority(res, which, who, prio)            \
  __sanitizer_syscall_post_impl_setpriority(                                   \
      res, (long long)(which), (long long)(who), (long long)(prio))
#define __sanitizer_syscall_pre_compat_30_socket(domain, type, protocol)       \
  __sanitizer_syscall_pre_impl_compat_30_socket(                               \
      (long long)(domain), (long long)(type), (long long)(protocol))
#define __sanitizer_syscall_post_compat_30_socket(res, domain, type, protocol) \
  __sanitizer_syscall_post_impl_compat_30_socket(                              \
      res, (long long)(domain), (long long)(type), (long long)(protocol))
#define __sanitizer_syscall_pre_connect(s, name, namelen)                      \
  __sanitizer_syscall_pre_impl_connect((long long)(s), (long long)(name),      \
                                       (long long)(namelen))
#define __sanitizer_syscall_post_connect(res, s, name, namelen)                \
  __sanitizer_syscall_post_impl_connect(                                       \
      res, (long long)(s), (long long)(name), (long long)(namelen))
#define __sanitizer_syscall_pre_compat_43_oaccept(s, name, anamelen)           \
  __sanitizer_syscall_pre_impl_compat_43_oaccept(                              \
      (long long)(s), (long long)(name), (long long)(anamelen))
#define __sanitizer_syscall_post_compat_43_oaccept(res, s, name, anamelen)     \
  __sanitizer_syscall_post_impl_compat_43_oaccept(                             \
      res, (long long)(s), (long long)(name), (long long)(anamelen))
#define __sanitizer_syscall_pre_getpriority(which, who)                        \
  __sanitizer_syscall_pre_impl_getpriority((long long)(which), (long long)(who))
#define __sanitizer_syscall_post_getpriority(res, which, who)                  \
  __sanitizer_syscall_post_impl_getpriority(res, (long long)(which),           \
                                            (long long)(who))
#define __sanitizer_syscall_pre_compat_43_osend(s, buf, len, flags)            \
  __sanitizer_syscall_pre_impl_compat_43_osend(                                \
      (long long)(s), (long long)(buf), (long long)(len), (long long)(flags))
#define __sanitizer_syscall_post_compat_43_osend(res, s, buf, len, flags)      \
  __sanitizer_syscall_post_impl_compat_43_osend(                               \
      res, (long long)(s), (long long)(buf), (long long)(len),                 \
      (long long)(flags))
#define __sanitizer_syscall_pre_compat_43_orecv(s, buf, len, flags)            \
  __sanitizer_syscall_pre_impl_compat_43_orecv(                                \
      (long long)(s), (long long)(buf), (long long)(len), (long long)(flags))
#define __sanitizer_syscall_post_compat_43_orecv(res, s, buf, len, flags)      \
  __sanitizer_syscall_post_impl_compat_43_orecv(                               \
      res, (long long)(s), (long long)(buf), (long long)(len),                 \
      (long long)(flags))
#define __sanitizer_syscall_pre_compat_13_sigreturn13(sigcntxp)                \
  __sanitizer_syscall_pre_impl_compat_13_sigreturn13((long long)(sigcntxp))
#define __sanitizer_syscall_post_compat_13_sigreturn13(res, sigcntxp)          \
  __sanitizer_syscall_post_impl_compat_13_sigreturn13(res,                     \
                                                      (long long)(sigcntxp))
#define __sanitizer_syscall_pre_bind(s, name, namelen)                         \
  __sanitizer_syscall_pre_impl_bind((long long)(s), (long long)(name),         \
                                    (long long)(namelen))
#define __sanitizer_syscall_post_bind(res, s, name, namelen)                   \
  __sanitizer_syscall_post_impl_bind(res, (long long)(s), (long long)(name),   \
                                     (long long)(namelen))
#define __sanitizer_syscall_pre_setsockopt(s, level, name, val, valsize)       \
  __sanitizer_syscall_pre_impl_setsockopt((long long)(s), (long long)(level),  \
                                          (long long)(name), (long long)(val), \
                                          (long long)(valsize))
#define __sanitizer_syscall_post_setsockopt(res, s, level, name, val, valsize) \
  __sanitizer_syscall_post_impl_setsockopt(                                    \
      res, (long long)(s), (long long)(level), (long long)(name),              \
      (long long)(val), (long long)(valsize))
#define __sanitizer_syscall_pre_listen(s, backlog)                             \
  __sanitizer_syscall_pre_impl_listen((long long)(s), (long long)(backlog))
#define __sanitizer_syscall_post_listen(res, s, backlog)                       \
  __sanitizer_syscall_post_impl_listen(res, (long long)(s),                    \
                                       (long long)(backlog))
/* syscall 107 has been skipped */
#define __sanitizer_syscall_pre_compat_43_osigvec(signum, nsv, osv)            \
  __sanitizer_syscall_pre_impl_compat_43_osigvec(                              \
      (long long)(signum), (long long)(nsv), (long long)(osv))
#define __sanitizer_syscall_post_compat_43_osigvec(res, signum, nsv, osv)      \
  __sanitizer_syscall_post_impl_compat_43_osigvec(                             \
      res, (long long)(signum), (long long)(nsv), (long long)(osv))
#define __sanitizer_syscall_pre_compat_43_osigblock(mask)                      \
  __sanitizer_syscall_pre_impl_compat_43_osigblock((long long)(mask))
#define __sanitizer_syscall_post_compat_43_osigblock(res, mask)                \
  __sanitizer_syscall_post_impl_compat_43_osigblock(res, (long long)(mask))
#define __sanitizer_syscall_pre_compat_43_osigsetmask(mask)                    \
  __sanitizer_syscall_pre_impl_compat_43_osigsetmask((long long)(mask))
#define __sanitizer_syscall_post_compat_43_osigsetmask(res, mask)              \
  __sanitizer_syscall_post_impl_compat_43_osigsetmask(res, (long long)(mask))
#define __sanitizer_syscall_pre_compat_13_sigsuspend13(mask)                   \
  __sanitizer_syscall_pre_impl_compat_13_sigsuspend13((long long)(mask))
#define __sanitizer_syscall_post_compat_13_sigsuspend13(res, mask)             \
  __sanitizer_syscall_post_impl_compat_13_sigsuspend13(res, (long long)(mask))
#define __sanitizer_syscall_pre_compat_43_osigstack(nss, oss)                  \
  __sanitizer_syscall_pre_impl_compat_43_osigstack((long long)(nss),           \
                                                   (long long)(oss))
#define __sanitizer_syscall_post_compat_43_osigstack(res, nss, oss)            \
  __sanitizer_syscall_post_impl_compat_43_osigstack(res, (long long)(nss),     \
                                                    (long long)(oss))
#define __sanitizer_syscall_pre_compat_43_orecvmsg(s, msg, flags)              \
  __sanitizer_syscall_pre_impl_compat_43_orecvmsg(                             \
      (long long)(s), (long long)(msg), (long long)(flags))
#define __sanitizer_syscall_post_compat_43_orecvmsg(res, s, msg, flags)        \
  __sanitizer_syscall_post_impl_compat_43_orecvmsg(                            \
      res, (long long)(s), (long long)(msg), (long long)(flags))
#define __sanitizer_syscall_pre_compat_43_osendmsg(s, msg, flags)              \
  __sanitizer_syscall_pre_impl_compat_43_osendmsg(                             \
      (long long)(s), (long long)(msg), (long long)(flags))
#define __sanitizer_syscall_post_compat_43_osendmsg(res, s, msg, flags)        \
  __sanitizer_syscall_post_impl_compat_43_osendmsg(                            \
      res, (long long)(s), (long long)(msg), (long long)(flags))
/* syscall 115 has been skipped */
#define __sanitizer_syscall_pre_compat_50_gettimeofday(tp, tzp)                \
  __sanitizer_syscall_pre_impl_compat_50_gettimeofday((long long)(tp),         \
                                                      (long long)(tzp))
#define __sanitizer_syscall_post_compat_50_gettimeofday(res, tp, tzp)          \
  __sanitizer_syscall_post_impl_compat_50_gettimeofday(res, (long long)(tp),   \
                                                       (long long)(tzp))
#define __sanitizer_syscall_pre_compat_50_getrusage(who, rusage)               \
  __sanitizer_syscall_pre_impl_compat_50_getrusage((long long)(who),           \
                                                   (long long)(rusage))
#define __sanitizer_syscall_post_compat_50_getrusage(res, who, rusage)         \
  __sanitizer_syscall_post_impl_compat_50_getrusage(res, (long long)(who),     \
                                                    (long long)(rusage))
#define __sanitizer_syscall_pre_getsockopt(s, level, name, val, avalsize)      \
  __sanitizer_syscall_pre_impl_getsockopt((long long)(s), (long long)(level),  \
                                          (long long)(name), (long long)(val), \
                                          (long long)(avalsize))
#define __sanitizer_syscall_post_getsockopt(res, s, level, name, val,          \
                                            avalsize)                          \
  __sanitizer_syscall_post_impl_getsockopt(                                    \
      res, (long long)(s), (long long)(level), (long long)(name),              \
      (long long)(val), (long long)(avalsize))
/* syscall 119 has been skipped */
#define __sanitizer_syscall_pre_readv(fd, iovp, iovcnt)                        \
  __sanitizer_syscall_pre_impl_readv((long long)(fd), (long long)(iovp),       \
                                     (long long)(iovcnt))
#define __sanitizer_syscall_post_readv(res, fd, iovp, iovcnt)                  \
  __sanitizer_syscall_post_impl_readv(res, (long long)(fd), (long long)(iovp), \
                                      (long long)(iovcnt))
#define __sanitizer_syscall_pre_writev(fd, iovp, iovcnt)                       \
  __sanitizer_syscall_pre_impl_writev((long long)(fd), (long long)(iovp),      \
                                      (long long)(iovcnt))
#define __sanitizer_syscall_post_writev(res, fd, iovp, iovcnt)                 \
  __sanitizer_syscall_post_impl_writev(res, (long long)(fd),                   \
                                       (long long)(iovp), (long long)(iovcnt))
#define __sanitizer_syscall_pre_compat_50_settimeofday(tv, tzp)                \
  __sanitizer_syscall_pre_impl_compat_50_settimeofday((long long)(tv),         \
                                                      (long long)(tzp))
#define __sanitizer_syscall_post_compat_50_settimeofday(res, tv, tzp)          \
  __sanitizer_syscall_post_impl_compat_50_settimeofday(res, (long long)(tv),   \
                                                       (long long)(tzp))
#define __sanitizer_syscall_pre_fchown(fd, uid, gid)                           \
  __sanitizer_syscall_pre_impl_fchown((long long)(fd), (long long)(uid),       \
                                      (long long)(gid))
#define __sanitizer_syscall_post_fchown(res, fd, uid, gid)                     \
  __sanitizer_syscall_post_impl_fchown(res, (long long)(fd), (long long)(uid), \
                                       (long long)(gid))
#define __sanitizer_syscall_pre_fchmod(fd, mode)                               \
  __sanitizer_syscall_pre_impl_fchmod((long long)(fd), (long long)(mode))
#define __sanitizer_syscall_post_fchmod(res, fd, mode)                         \
  __sanitizer_syscall_post_impl_fchmod(res, (long long)(fd), (long long)(mode))
#define __sanitizer_syscall_pre_compat_43_orecvfrom(s, buf, len, flags, from,  \
                                                    fromlenaddr)               \
  __sanitizer_syscall_pre_impl_compat_43_orecvfrom(                            \
      (long long)(s), (long long)(buf), (long long)(len), (long long)(flags),  \
      (long long)(from), (long long)(fromlenaddr))
#define __sanitizer_syscall_post_compat_43_orecvfrom(res, s, buf, len, flags,  \
                                                     from, fromlenaddr)        \
  __sanitizer_syscall_post_impl_compat_43_orecvfrom(                           \
      res, (long long)(s), (long long)(buf), (long long)(len),                 \
      (long long)(flags), (long long)(from), (long long)(fromlenaddr))
#define __sanitizer_syscall_pre_setreuid(ruid, euid)                           \
  __sanitizer_syscall_pre_impl_setreuid((long long)(ruid), (long long)(euid))
#define __sanitizer_syscall_post_setreuid(res, ruid, euid)                     \
  __sanitizer_syscall_post_impl_setreuid(res, (long long)(ruid),               \
                                         (long long)(euid))
#define __sanitizer_syscall_pre_setregid(rgid, egid)                           \
  __sanitizer_syscall_pre_impl_setregid((long long)(rgid), (long long)(egid))
#define __sanitizer_syscall_post_setregid(res, rgid, egid)                     \
  __sanitizer_syscall_post_impl_setregid(res, (long long)(rgid),               \
                                         (long long)(egid))
#define __sanitizer_syscall_pre_rename(from, to)                               \
  __sanitizer_syscall_pre_impl_rename((long long)(from), (long long)(to))
#define __sanitizer_syscall_post_rename(res, from, to)                         \
  __sanitizer_syscall_post_impl_rename(res, (long long)(from), (long long)(to))
#define __sanitizer_syscall_pre_compat_43_otruncate(path, length)              \
  __sanitizer_syscall_pre_impl_compat_43_otruncate((long long)(path),          \
                                                   (long long)(length))
#define __sanitizer_syscall_post_compat_43_otruncate(res, path, length)        \
  __sanitizer_syscall_post_impl_compat_43_otruncate(res, (long long)(path),    \
                                                    (long long)(length))
#define __sanitizer_syscall_pre_compat_43_oftruncate(fd, length)               \
  __sanitizer_syscall_pre_impl_compat_43_oftruncate((long long)(fd),           \
                                                    (long long)(length))
#define __sanitizer_syscall_post_compat_43_oftruncate(res, fd, length)         \
  __sanitizer_syscall_post_impl_compat_43_oftruncate(res, (long long)(fd),     \
                                                     (long long)(length))
#define __sanitizer_syscall_pre_flock(fd, how)                                 \
  __sanitizer_syscall_pre_impl_flock((long long)(fd), (long long)(how))
#define __sanitizer_syscall_post_flock(res, fd, how)                           \
  __sanitizer_syscall_post_impl_flock(res, (long long)(fd), (long long)(how))
#define __sanitizer_syscall_pre_mkfifo(path, mode)                             \
  __sanitizer_syscall_pre_impl_mkfifo((long long)(path), (long long)(mode))
#define __sanitizer_syscall_post_mkfifo(res, path, mode)                       \
  __sanitizer_syscall_post_impl_mkfifo(res, (long long)(path),                 \
                                       (long long)(mode))
#define __sanitizer_syscall_pre_sendto(s, buf, len, flags, to, tolen)          \
  __sanitizer_syscall_pre_impl_sendto((long long)(s), (long long)(buf),        \
                                      (long long)(len), (long long)(flags),    \
                                      (long long)(to), (long long)(tolen))
#define __sanitizer_syscall_post_sendto(res, s, buf, len, flags, to, tolen)    \
  __sanitizer_syscall_post_impl_sendto(res, (long long)(s), (long long)(buf),  \
                                       (long long)(len), (long long)(flags),   \
                                       (long long)(to), (long long)(tolen))
#define __sanitizer_syscall_pre_shutdown(s, how)                               \
  __sanitizer_syscall_pre_impl_shutdown((long long)(s), (long long)(how))
#define __sanitizer_syscall_post_shutdown(res, s, how)                         \
  __sanitizer_syscall_post_impl_shutdown(res, (long long)(s), (long long)(how))
#define __sanitizer_syscall_pre_socketpair(domain, type, protocol, rsv)        \
  __sanitizer_syscall_pre_impl_socketpair(                                     \
      (long long)(domain), (long long)(type), (long long)(protocol),           \
      (long long)(rsv))
#define __sanitizer_syscall_post_socketpair(res, domain, type, protocol, rsv)  \
  __sanitizer_syscall_post_impl_socketpair(                                    \
      res, (long long)(domain), (long long)(type), (long long)(protocol),      \
      (long long)(rsv))
#define __sanitizer_syscall_pre_mkdir(path, mode)                              \
  __sanitizer_syscall_pre_impl_mkdir((long long)(path), (long long)(mode))
#define __sanitizer_syscall_post_mkdir(res, path, mode)                        \
  __sanitizer_syscall_post_impl_mkdir(res, (long long)(path), (long long)(mode))
#define __sanitizer_syscall_pre_rmdir(path)                                    \
  __sanitizer_syscall_pre_impl_rmdir((long long)(path))
#define __sanitizer_syscall_post_rmdir(res, path)                              \
  __sanitizer_syscall_post_impl_rmdir(res, (long long)(path))
#define __sanitizer_syscall_pre_compat_50_utimes(path, tptr)                   \
  __sanitizer_syscall_pre_impl_compat_50_utimes((long long)(path),             \
                                                (long long)(tptr))
#define __sanitizer_syscall_post_compat_50_utimes(res, path, tptr)             \
  __sanitizer_syscall_post_impl_compat_50_utimes(res, (long long)(path),       \
                                                 (long long)(tptr))
/* syscall 139 has been skipped */
#define __sanitizer_syscall_pre_compat_50_adjtime(delta, olddelta)             \
  __sanitizer_syscall_pre_impl_compat_50_adjtime((long long)(delta),           \
                                                 (long long)(olddelta))
#define __sanitizer_syscall_post_compat_50_adjtime(res, delta, olddelta)       \
  __sanitizer_syscall_post_impl_compat_50_adjtime(res, (long long)(delta),     \
                                                  (long long)(olddelta))
#define __sanitizer_syscall_pre_compat_43_ogetpeername(fdes, asa, alen)        \
  __sanitizer_syscall_pre_impl_compat_43_ogetpeername(                         \
      (long long)(fdes), (long long)(asa), (long long)(alen))
#define __sanitizer_syscall_post_compat_43_ogetpeername(res, fdes, asa, alen)  \
  __sanitizer_syscall_post_impl_compat_43_ogetpeername(                        \
      res, (long long)(fdes), (long long)(asa), (long long)(alen))
#define __sanitizer_syscall_pre_compat_43_ogethostid()                         \
  __sanitizer_syscall_pre_impl_compat_43_ogethostid()
#define __sanitizer_syscall_post_compat_43_ogethostid(res)                     \
  __sanitizer_syscall_post_impl_compat_43_ogethostid(res)
#define __sanitizer_syscall_pre_compat_43_osethostid(hostid)                   \
  __sanitizer_syscall_pre_impl_compat_43_osethostid((long long)(hostid))
#define __sanitizer_syscall_post_compat_43_osethostid(res, hostid)             \
  __sanitizer_syscall_post_impl_compat_43_osethostid(res, (long long)(hostid))
#define __sanitizer_syscall_pre_compat_43_ogetrlimit(which, rlp)               \
  __sanitizer_syscall_pre_impl_compat_43_ogetrlimit((long long)(which),        \
                                                    (long long)(rlp))
#define __sanitizer_syscall_post_compat_43_ogetrlimit(res, which, rlp)         \
  __sanitizer_syscall_post_impl_compat_43_ogetrlimit(res, (long long)(which),  \
                                                     (long long)(rlp))
#define __sanitizer_syscall_pre_compat_43_osetrlimit(which, rlp)               \
  __sanitizer_syscall_pre_impl_compat_43_osetrlimit((long long)(which),        \
                                                    (long long)(rlp))
#define __sanitizer_syscall_post_compat_43_osetrlimit(res, which, rlp)         \
  __sanitizer_syscall_post_impl_compat_43_osetrlimit(res, (long long)(which),  \
                                                     (long long)(rlp))
#define __sanitizer_syscall_pre_compat_43_okillpg(pgid, signum)                \
  __sanitizer_syscall_pre_impl_compat_43_okillpg((long long)(pgid),            \
                                                 (long long)(signum))
#define __sanitizer_syscall_post_compat_43_okillpg(res, pgid, signum)          \
  __sanitizer_syscall_post_impl_compat_43_okillpg(res, (long long)(pgid),      \
                                                  (long long)(signum))
#define __sanitizer_syscall_pre_setsid() __sanitizer_syscall_pre_impl_setsid()
#define __sanitizer_syscall_post_setsid(res)                                   \
  __sanitizer_syscall_post_impl_setsid(res)
#define __sanitizer_syscall_pre_compat_50_quotactl(path, cmd, uid, arg)        \
  __sanitizer_syscall_pre_impl_compat_50_quotactl(                             \
      (long long)(path), (long long)(cmd), (long long)(uid), (long long)(arg))
#define __sanitizer_syscall_post_compat_50_quotactl(res, path, cmd, uid, arg)  \
  __sanitizer_syscall_post_impl_compat_50_quotactl(                            \
      res, (long long)(path), (long long)(cmd), (long long)(uid),              \
      (long long)(arg))
#define __sanitizer_syscall_pre_compat_43_oquota()                             \
  __sanitizer_syscall_pre_impl_compat_43_oquota()
#define __sanitizer_syscall_post_compat_43_oquota(res)                         \
  __sanitizer_syscall_post_impl_compat_43_oquota(res)
#define __sanitizer_syscall_pre_compat_43_ogetsockname(fdec, asa, alen)        \
  __sanitizer_syscall_pre_impl_compat_43_ogetsockname(                         \
      (long long)(fdec), (long long)(asa), (long long)(alen))
#define __sanitizer_syscall_post_compat_43_ogetsockname(res, fdec, asa, alen)  \
  __sanitizer_syscall_post_impl_compat_43_ogetsockname(                        \
      res, (long long)(fdec), (long long)(asa), (long long)(alen))
/* syscall 151 has been skipped */
/* syscall 152 has been skipped */
/* syscall 153 has been skipped */
/* syscall 154 has been skipped */
#define __sanitizer_syscall_pre_nfssvc(flag, argp)                             \
  __sanitizer_syscall_pre_impl_nfssvc((long long)(flag), (long long)(argp))
#define __sanitizer_syscall_post_nfssvc(res, flag, argp)                       \
  __sanitizer_syscall_post_impl_nfssvc(res, (long long)(flag),                 \
                                       (long long)(argp))
#define __sanitizer_syscall_pre_compat_43_ogetdirentries(fd, buf, count,       \
                                                         basep)                \
  __sanitizer_syscall_pre_impl_compat_43_ogetdirentries(                       \
      (long long)(fd), (long long)(buf), (long long)(count),                   \
      (long long)(basep))
#define __sanitizer_syscall_post_compat_43_ogetdirentries(res, fd, buf, count, \
                                                          basep)               \
  __sanitizer_syscall_post_impl_compat_43_ogetdirentries(                      \
      res, (long long)(fd), (long long)(buf), (long long)(count),              \
      (long long)(basep))
#define __sanitizer_syscall_pre_compat_20_statfs(path, buf)                    \
  __sanitizer_syscall_pre_impl_compat_20_statfs((long long)(path),             \
                                                (long long)(buf))
#define __sanitizer_syscall_post_compat_20_statfs(res, path, buf)              \
  __sanitizer_syscall_post_impl_compat_20_statfs(res, (long long)(path),       \
                                                 (long long)(buf))
#define __sanitizer_syscall_pre_compat_20_fstatfs(fd, buf)                     \
  __sanitizer_syscall_pre_impl_compat_20_fstatfs((long long)(fd),              \
                                                 (long long)(buf))
#define __sanitizer_syscall_post_compat_20_fstatfs(res, fd, buf)               \
  __sanitizer_syscall_post_impl_compat_20_fstatfs(res, (long long)(fd),        \
                                                  (long long)(buf))
/* syscall 159 has been skipped */
/* syscall 160 has been skipped */
#define __sanitizer_syscall_pre_compat_30_getfh(fname, fhp)                    \
  __sanitizer_syscall_pre_impl_compat_30_getfh((long long)(fname),             \
                                               (long long)(fhp))
#define __sanitizer_syscall_post_compat_30_getfh(res, fname, fhp)              \
  __sanitizer_syscall_post_impl_compat_30_getfh(res, (long long)(fname),       \
                                                (long long)(fhp))
#define __sanitizer_syscall_pre_compat_09_ogetdomainname(domainname, len)      \
  __sanitizer_syscall_pre_impl_compat_09_ogetdomainname(                       \
      (long long)(domainname), (long long)(len))
#define __sanitizer_syscall_post_compat_09_ogetdomainname(res, domainname,     \
                                                          len)                 \
  __sanitizer_syscall_post_impl_compat_09_ogetdomainname(                      \
      res, (long long)(domainname), (long long)(len))
#define __sanitizer_syscall_pre_compat_09_osetdomainname(domainname, len)      \
  __sanitizer_syscall_pre_impl_compat_09_osetdomainname(                       \
      (long long)(domainname), (long long)(len))
#define __sanitizer_syscall_post_compat_09_osetdomainname(res, domainname,     \
                                                          len)                 \
  __sanitizer_syscall_post_impl_compat_09_osetdomainname(                      \
      res, (long long)(domainname), (long long)(len))
#define __sanitizer_syscall_pre_compat_09_ouname(name)                         \
  __sanitizer_syscall_pre_impl_compat_09_ouname((long long)(name))
#define __sanitizer_syscall_post_compat_09_ouname(res, name)                   \
  __sanitizer_syscall_post_impl_compat_09_ouname(res, (long long)(name))
#define __sanitizer_syscall_pre_sysarch(op, parms)                             \
  __sanitizer_syscall_pre_impl_sysarch((long long)(op), (long long)(parms))
#define __sanitizer_syscall_post_sysarch(res, op, parms)                       \
  __sanitizer_syscall_post_impl_sysarch(res, (long long)(op),                  \
                                        (long long)(parms))
/* syscall 166 has been skipped */
/* syscall 167 has been skipped */
/* syscall 168 has been skipped */
#if !defined(_LP64)
#define __sanitizer_syscall_pre_compat_10_osemsys(which, a2, a3, a4, a5)       \
  __sanitizer_syscall_pre_impl_compat_10_osemsys(                              \
      (long long)(which), (long long)(a2), (long long)(a3), (long long)(a4),   \
      (long long)(a5))
#define __sanitizer_syscall_post_compat_10_osemsys(res, which, a2, a3, a4, a5) \
  __sanitizer_syscall_post_impl_compat_10_osemsys(                             \
      res, (long long)(which), (long long)(a2), (long long)(a3),               \
      (long long)(a4), (long long)(a5))
#else
/* syscall 169 has been skipped */
#endif
#if !defined(_LP64)
#define __sanitizer_syscall_pre_compat_10_omsgsys(which, a2, a3, a4, a5, a6)   \
  __sanitizer_syscall_pre_impl_compat_10_omsgsys(                              \
      (long long)(which), (long long)(a2), (long long)(a3), (long long)(a4),   \
      (long long)(a5), (long long)(a6))
#define __sanitizer_syscall_post_compat_10_omsgsys(res, which, a2, a3, a4, a5, \
                                                   a6)                         \
  __sanitizer_syscall_post_impl_compat_10_omsgsys(                             \
      res, (long long)(which), (long long)(a2), (long long)(a3),               \
      (long long)(a4), (long long)(a5), (long long)(a6))
#else
/* syscall 170 has been skipped */
#endif
#if !defined(_LP64)
#define __sanitizer_syscall_pre_compat_10_oshmsys(which, a2, a3, a4)           \
  __sanitizer_syscall_pre_impl_compat_10_oshmsys(                              \
      (long long)(which), (long long)(a2), (long long)(a3), (long long)(a4))
#define __sanitizer_syscall_post_compat_10_oshmsys(res, which, a2, a3, a4)     \
  __sanitizer_syscall_post_impl_compat_10_oshmsys(                             \
      res, (long long)(which), (long long)(a2), (long long)(a3),               \
      (long long)(a4))
#else
/* syscall 171 has been skipped */
#endif
/* syscall 172 has been skipped */
#define __sanitizer_syscall_pre_pread(fd, buf, nbyte, PAD, offset)             \
  __sanitizer_syscall_pre_impl_pread((long long)(fd), (long long)(buf),        \
                                     (long long)(nbyte), (long long)(PAD),     \
                                     (long long)(offset))
#define __sanitizer_syscall_post_pread(res, fd, buf, nbyte, PAD, offset)       \
  __sanitizer_syscall_post_impl_pread(res, (long long)(fd), (long long)(buf),  \
                                      (long long)(nbyte), (long long)(PAD),    \
                                      (long long)(offset))
#define __sanitizer_syscall_pre_pwrite(fd, buf, nbyte, PAD, offset)            \
  __sanitizer_syscall_pre_impl_pwrite((long long)(fd), (long long)(buf),       \
                                      (long long)(nbyte), (long long)(PAD),    \
                                      (long long)(offset))
#define __sanitizer_syscall_post_pwrite(res, fd, buf, nbyte, PAD, offset)      \
  __sanitizer_syscall_post_impl_pwrite(res, (long long)(fd), (long long)(buf), \
                                       (long long)(nbyte), (long long)(PAD),   \
                                       (long long)(offset))
#define __sanitizer_syscall_pre_compat_30_ntp_gettime(ntvp)                    \
  __sanitizer_syscall_pre_impl_compat_30_ntp_gettime((long long)(ntvp))
#define __sanitizer_syscall_post_compat_30_ntp_gettime(res, ntvp)              \
  __sanitizer_syscall_post_impl_compat_30_ntp_gettime(res, (long long)(ntvp))
#if defined(NTP) || !defined(_KERNEL_OPT)
#define __sanitizer_syscall_pre_ntp_adjtime(tp)                                \
  __sanitizer_syscall_pre_impl_ntp_adjtime((long long)(tp))
#define __sanitizer_syscall_post_ntp_adjtime(res, tp)                          \
  __sanitizer_syscall_post_impl_ntp_adjtime(res, (long long)(tp))
#else
/* syscall 176 has been skipped */
#endif
/* syscall 177 has been skipped */
/* syscall 178 has been skipped */
/* syscall 179 has been skipped */
/* syscall 180 has been skipped */
#define __sanitizer_syscall_pre_setgid(gid)                                    \
  __sanitizer_syscall_pre_impl_setgid((long long)(gid))
#define __sanitizer_syscall_post_setgid(res, gid)                              \
  __sanitizer_syscall_post_impl_setgid(res, (long long)(gid))
#define __sanitizer_syscall_pre_setegid(egid)                                  \
  __sanitizer_syscall_pre_impl_setegid((long long)(egid))
#define __sanitizer_syscall_post_setegid(res, egid)                            \
  __sanitizer_syscall_post_impl_setegid(res, (long long)(egid))
#define __sanitizer_syscall_pre_seteuid(euid)                                  \
  __sanitizer_syscall_pre_impl_seteuid((long long)(euid))
#define __sanitizer_syscall_post_seteuid(res, euid)                            \
  __sanitizer_syscall_post_impl_seteuid(res, (long long)(euid))
#define __sanitizer_syscall_pre_lfs_bmapv(fsidp, blkiov, blkcnt)               \
  __sanitizer_syscall_pre_impl_lfs_bmapv(                                      \
      (long long)(fsidp), (long long)(blkiov), (long long)(blkcnt))
#define __sanitizer_syscall_post_lfs_bmapv(res, fsidp, blkiov, blkcnt)         \
  __sanitizer_syscall_post_impl_lfs_bmapv(                                     \
      res, (long long)(fsidp), (long long)(blkiov), (long long)(blkcnt))
#define __sanitizer_syscall_pre_lfs_markv(fsidp, blkiov, blkcnt)               \
  __sanitizer_syscall_pre_impl_lfs_markv(                                      \
      (long long)(fsidp), (long long)(blkiov), (long long)(blkcnt))
#define __sanitizer_syscall_post_lfs_markv(res, fsidp, blkiov, blkcnt)         \
  __sanitizer_syscall_post_impl_lfs_markv(                                     \
      res, (long long)(fsidp), (long long)(blkiov), (long long)(blkcnt))
#define __sanitizer_syscall_pre_lfs_segclean(fsidp, segment)                   \
  __sanitizer_syscall_pre_impl_lfs_segclean((long long)(fsidp),                \
                                            (long long)(segment))
#define __sanitizer_syscall_post_lfs_segclean(res, fsidp, segment)             \
  __sanitizer_syscall_post_impl_lfs_segclean(res, (long long)(fsidp),          \
                                             (long long)(segment))
#define __sanitizer_syscall_pre_compat_50_lfs_segwait(fsidp, tv)               \
  __sanitizer_syscall_pre_impl_compat_50_lfs_segwait((long long)(fsidp),       \
                                                     (long long)(tv))
#define __sanitizer_syscall_post_compat_50_lfs_segwait(res, fsidp, tv)         \
  __sanitizer_syscall_post_impl_compat_50_lfs_segwait(res, (long long)(fsidp), \
                                                      (long long)(tv))
#define __sanitizer_syscall_pre_compat_12_stat12(path, ub)                     \
  __sanitizer_syscall_pre_impl_compat_12_stat12((long long)(path),             \
                                                (long long)(ub))
#define __sanitizer_syscall_post_compat_12_stat12(res, path, ub)               \
  __sanitizer_syscall_post_impl_compat_12_stat12(res, (long long)(path),       \
                                                 (long long)(ub))
#define __sanitizer_syscall_pre_compat_12_fstat12(fd, sb)                      \
  __sanitizer_syscall_pre_impl_compat_12_fstat12((long long)(fd),              \
                                                 (long long)(sb))
#define __sanitizer_syscall_post_compat_12_fstat12(res, fd, sb)                \
  __sanitizer_syscall_post_impl_compat_12_fstat12(res, (long long)(fd),        \
                                                  (long long)(sb))
#define __sanitizer_syscall_pre_compat_12_lstat12(path, ub)                    \
  __sanitizer_syscall_pre_impl_compat_12_lstat12((long long)(path),            \
                                                 (long long)(ub))
#define __sanitizer_syscall_post_compat_12_lstat12(res, path, ub)              \
  __sanitizer_syscall_post_impl_compat_12_lstat12(res, (long long)(path),      \
                                                  (long long)(ub))
#define __sanitizer_syscall_pre_pathconf(path, name)                           \
  __sanitizer_syscall_pre_impl_pathconf((long long)(path), (long long)(name))
#define __sanitizer_syscall_post_pathconf(res, path, name)                     \
  __sanitizer_syscall_post_impl_pathconf(res, (long long)(path),               \
                                         (long long)(name))
#define __sanitizer_syscall_pre_fpathconf(fd, name)                            \
  __sanitizer_syscall_pre_impl_fpathconf((long long)(fd), (long long)(name))
#define __sanitizer_syscall_post_fpathconf(res, fd, name)                      \
  __sanitizer_syscall_post_impl_fpathconf(res, (long long)(fd),                \
                                          (long long)(name))
#define __sanitizer_syscall_pre_getsockopt2(s, level, name, val, avalsize)     \
  __sanitizer_syscall_pre_impl_getsockopt2(                                    \
      (long long)(s), (long long)(level), (long long)(name), (long long)(val), \
      (long long)(avalsize))
#define __sanitizer_syscall_post_getsockopt2(res, s, level, name, val,         \
                                             avalsize)                         \
  __sanitizer_syscall_post_impl_getsockopt2(                                   \
      res, (long long)(s), (long long)(level), (long long)(name),              \
      (long long)(val), (long long)(avalsize))
#define __sanitizer_syscall_pre_getrlimit(which, rlp)                          \
  __sanitizer_syscall_pre_impl_getrlimit((long long)(which), (long long)(rlp))
#define __sanitizer_syscall_post_getrlimit(res, which, rlp)                    \
  __sanitizer_syscall_post_impl_getrlimit(res, (long long)(which),             \
                                          (long long)(rlp))
#define __sanitizer_syscall_pre_setrlimit(which, rlp)                          \
  __sanitizer_syscall_pre_impl_setrlimit((long long)(which), (long long)(rlp))
#define __sanitizer_syscall_post_setrlimit(res, which, rlp)                    \
  __sanitizer_syscall_post_impl_setrlimit(res, (long long)(which),             \
                                          (long long)(rlp))
#define __sanitizer_syscall_pre_compat_12_getdirentries(fd, buf, count, basep) \
  __sanitizer_syscall_pre_impl_compat_12_getdirentries(                        \
      (long long)(fd), (long long)(buf), (long long)(count),                   \
      (long long)(basep))
#define __sanitizer_syscall_post_compat_12_getdirentries(res, fd, buf, count,  \
                                                         basep)                \
  __sanitizer_syscall_post_impl_compat_12_getdirentries(                       \
      res, (long long)(fd), (long long)(buf), (long long)(count),              \
      (long long)(basep))
#define __sanitizer_syscall_pre_mmap(addr, len, prot, flags, fd, PAD, pos)     \
  __sanitizer_syscall_pre_impl_mmap(                                           \
      (long long)(addr), (long long)(len), (long long)(prot),                  \
      (long long)(flags), (long long)(fd), (long long)(PAD), (long long)(pos))
#define __sanitizer_syscall_post_mmap(res, addr, len, prot, flags, fd, PAD,    \
                                      pos)                                     \
  __sanitizer_syscall_post_impl_mmap(                                          \
      res, (long long)(addr), (long long)(len), (long long)(prot),             \
      (long long)(flags), (long long)(fd), (long long)(PAD), (long long)(pos))
#define __sanitizer_syscall_pre___syscall(code, arg0, arg1, arg2, arg3, arg4,  \
                                          arg5, arg6, arg7)                    \
  __sanitizer_syscall_pre_impl___syscall(                                      \
      (long long)(code), (long long)(arg0), (long long)(arg1),                 \
      (long long)(arg2), (long long)(arg3), (long long)(arg4),                 \
      (long long)(arg5), (long long)(arg6), (long long)(arg7))
#define __sanitizer_syscall_post___syscall(res, code, arg0, arg1, arg2, arg3,  \
                                           arg4, arg5, arg6, arg7)             \
  __sanitizer_syscall_post_impl___syscall(                                     \
      res, (long long)(code), (long long)(arg0), (long long)(arg1),            \
      (long long)(arg2), (long long)(arg3), (long long)(arg4),                 \
      (long long)(arg5), (long long)(arg6), (long long)(arg7))
#define __sanitizer_syscall_pre_lseek(fd, PAD, offset, whence)                 \
  __sanitizer_syscall_pre_impl_lseek((long long)(fd), (long long)(PAD),        \
                                     (long long)(offset), (long long)(whence))
#define __sanitizer_syscall_post_lseek(res, fd, PAD, offset, whence)           \
  __sanitizer_syscall_post_impl_lseek(res, (long long)(fd), (long long)(PAD),  \
                                      (long long)(offset),                     \
                                      (long long)(whence))
#define __sanitizer_syscall_pre_truncate(path, PAD, length)                    \
  __sanitizer_syscall_pre_impl_truncate((long long)(path), (long long)(PAD),   \
                                        (long long)(length))
#define __sanitizer_syscall_post_truncate(res, path, PAD, length)              \
  __sanitizer_syscall_post_impl_truncate(                                      \
      res, (long long)(path), (long long)(PAD), (long long)(length))
#define __sanitizer_syscall_pre_ftruncate(fd, PAD, length)                     \
  __sanitizer_syscall_pre_impl_ftruncate((long long)(fd), (long long)(PAD),    \
                                         (long long)(length))
#define __sanitizer_syscall_post_ftruncate(res, fd, PAD, length)               \
  __sanitizer_syscall_post_impl_ftruncate(                                     \
      res, (long long)(fd), (long long)(PAD), (long long)(length))
#define __sanitizer_syscall_pre___sysctl(name, namelen, oldv, oldlenp, newv,   \
                                         newlen)                               \
  __sanitizer_syscall_pre_impl___sysctl(                                       \
      (long long)(name), (long long)(namelen), (long long)(oldv),              \
      (long long)(oldlenp), (long long)(newv), (long long)(newlen))
#define __sanitizer_syscall_post___sysctl(res, name, namelen, oldv, oldlenp,   \
                                          newv, newlen)                        \
  __sanitizer_syscall_post_impl___sysctl(                                      \
      res, (long long)(name), (long long)(namelen), (long long)(oldv),         \
      (long long)(oldlenp), (long long)(newv), (long long)(newlen))
#define __sanitizer_syscall_pre_mlock(addr, len)                               \
  __sanitizer_syscall_pre_impl_mlock((long long)(addr), (long long)(len))
#define __sanitizer_syscall_post_mlock(res, addr, len)                         \
  __sanitizer_syscall_post_impl_mlock(res, (long long)(addr), (long long)(len))
#define __sanitizer_syscall_pre_munlock(addr, len)                             \
  __sanitizer_syscall_pre_impl_munlock((long long)(addr), (long long)(len))
#define __sanitizer_syscall_post_munlock(res, addr, len)                       \
  __sanitizer_syscall_post_impl_munlock(res, (long long)(addr),                \
                                        (long long)(len))
#define __sanitizer_syscall_pre_undelete(path)                                 \
  __sanitizer_syscall_pre_impl_undelete((long long)(path))
#define __sanitizer_syscall_post_undelete(res, path)                           \
  __sanitizer_syscall_post_impl_undelete(res, (long long)(path))
#define __sanitizer_syscall_pre_compat_50_futimes(fd, tptr)                    \
  __sanitizer_syscall_pre_impl_compat_50_futimes((long long)(fd),              \
                                                 (long long)(tptr))
#define __sanitizer_syscall_post_compat_50_futimes(res, fd, tptr)              \
  __sanitizer_syscall_post_impl_compat_50_futimes(res, (long long)(fd),        \
                                                  (long long)(tptr))
#define __sanitizer_syscall_pre_getpgid(pid)                                   \
  __sanitizer_syscall_pre_impl_getpgid((long long)(pid))
#define __sanitizer_syscall_post_getpgid(res, pid)                             \
  __sanitizer_syscall_post_impl_getpgid(res, (long long)(pid))
#define __sanitizer_syscall_pre_reboot(opt, bootstr)                           \
  __sanitizer_syscall_pre_impl_reboot((long long)(opt), (long long)(bootstr))
#define __sanitizer_syscall_post_reboot(res, opt, bootstr)                     \
  __sanitizer_syscall_post_impl_reboot(res, (long long)(opt),                  \
                                       (long long)(bootstr))
#define __sanitizer_syscall_pre_poll(fds, nfds, timeout)                       \
  __sanitizer_syscall_pre_impl_poll((long long)(fds), (long long)(nfds),       \
                                    (long long)(timeout))
#define __sanitizer_syscall_post_poll(res, fds, nfds, timeout)                 \
  __sanitizer_syscall_post_impl_poll(res, (long long)(fds), (long long)(nfds), \
                                     (long long)(timeout))
#define __sanitizer_syscall_pre_afssys(id, a1, a2, a3, a4, a5, a6)             \
  __sanitizer_syscall_pre_impl_afssys(                                         \
      (long long)(id), (long long)(a1), (long long)(a2), (long long)(a3),      \
      (long long)(a4), (long long)(a5), (long long)(a6))
#define __sanitizer_syscall_post_afssys(res, id, a1, a2, a3, a4, a5, a6)       \
  __sanitizer_syscall_post_impl_afssys(                                        \
      res, (long long)(id), (long long)(a1), (long long)(a2), (long long)(a3), \
      (long long)(a4), (long long)(a5), (long long)(a6))
/* syscall 211 has been skipped */
/* syscall 212 has been skipped */
/* syscall 213 has been skipped */
/* syscall 214 has been skipped */
/* syscall 215 has been skipped */
/* syscall 216 has been skipped */
/* syscall 217 has been skipped */
/* syscall 218 has been skipped */
/* syscall 219 has been skipped */
#define __sanitizer_syscall_pre_compat_14___semctl(semid, semnum, cmd, arg)    \
  __sanitizer_syscall_pre_impl_compat_14___semctl(                             \
      (long long)(semid), (long long)(semnum), (long long)(cmd),               \
      (long long)(arg))
#define __sanitizer_syscall_post_compat_14___semctl(res, semid, semnum, cmd,   \
                                                    arg)                       \
  __sanitizer_syscall_post_impl_compat_14___semctl(                            \
      res, (long long)(semid), (long long)(semnum), (long long)(cmd),          \
      (long long)(arg))
#define __sanitizer_syscall_pre_semget(key, nsems, semflg)                     \
  __sanitizer_syscall_pre_impl_semget((long long)(key), (long long)(nsems),    \
                                      (long long)(semflg))
#define __sanitizer_syscall_post_semget(res, key, nsems, semflg)               \
  __sanitizer_syscall_post_impl_semget(                                        \
      res, (long long)(key), (long long)(nsems), (long long)(semflg))
#define __sanitizer_syscall_pre_semop(semid, sops, nsops)                      \
  __sanitizer_syscall_pre_impl_semop((long long)(semid), (long long)(sops),    \
                                     (long long)(nsops))
#define __sanitizer_syscall_post_semop(res, semid, sops, nsops)                \
  __sanitizer_syscall_post_impl_semop(res, (long long)(semid),                 \
                                      (long long)(sops), (long long)(nsops))
#define __sanitizer_syscall_pre_semconfig(flag)                                \
  __sanitizer_syscall_pre_impl_semconfig((long long)(flag))
#define __sanitizer_syscall_post_semconfig(res, flag)                          \
  __sanitizer_syscall_post_impl_semconfig(res, (long long)(flag))
#define __sanitizer_syscall_pre_compat_14_msgctl(msqid, cmd, buf)              \
  __sanitizer_syscall_pre_impl_compat_14_msgctl(                               \
      (long long)(msqid), (long long)(cmd), (long long)(buf))
#define __sanitizer_syscall_post_compat_14_msgctl(res, msqid, cmd, buf)        \
  __sanitizer_syscall_post_impl_compat_14_msgctl(                              \
      res, (long long)(msqid), (long long)(cmd), (long long)(buf))
#define __sanitizer_syscall_pre_msgget(key, msgflg)                            \
  __sanitizer_syscall_pre_impl_msgget((long long)(key), (long long)(msgflg))
#define __sanitizer_syscall_post_msgget(res, key, msgflg)                      \
  __sanitizer_syscall_post_impl_msgget(res, (long long)(key),                  \
                                       (long long)(msgflg))
#define __sanitizer_syscall_pre_msgsnd(msqid, msgp, msgsz, msgflg)             \
  __sanitizer_syscall_pre_impl_msgsnd((long long)(msqid), (long long)(msgp),   \
                                      (long long)(msgsz), (long long)(msgflg))
#define __sanitizer_syscall_post_msgsnd(res, msqid, msgp, msgsz, msgflg)       \
  __sanitizer_syscall_post_impl_msgsnd(res, (long long)(msqid),                \
                                       (long long)(msgp), (long long)(msgsz),  \
                                       (long long)(msgflg))
#define __sanitizer_syscall_pre_msgrcv(msqid, msgp, msgsz, msgtyp, msgflg)     \
  __sanitizer_syscall_pre_impl_msgrcv((long long)(msqid), (long long)(msgp),   \
                                      (long long)(msgsz), (long long)(msgtyp), \
                                      (long long)(msgflg))
#define __sanitizer_syscall_post_msgrcv(res, msqid, msgp, msgsz, msgtyp,       \
                                        msgflg)                                \
  __sanitizer_syscall_post_impl_msgrcv(                                        \
      res, (long long)(msqid), (long long)(msgp), (long long)(msgsz),          \
      (long long)(msgtyp), (long long)(msgflg))
#define __sanitizer_syscall_pre_shmat(shmid, shmaddr, shmflg)                  \
  __sanitizer_syscall_pre_impl_shmat((long long)(shmid), (long long)(shmaddr), \
                                     (long long)(shmflg))
#define __sanitizer_syscall_post_shmat(res, shmid, shmaddr, shmflg)            \
  __sanitizer_syscall_post_impl_shmat(                                         \
      res, (long long)(shmid), (long long)(shmaddr), (long long)(shmflg))
#define __sanitizer_syscall_pre_compat_14_shmctl(shmid, cmd, buf)              \
  __sanitizer_syscall_pre_impl_compat_14_shmctl(                               \
      (long long)(shmid), (long long)(cmd), (long long)(buf))
#define __sanitizer_syscall_post_compat_14_shmctl(res, shmid, cmd, buf)        \
  __sanitizer_syscall_post_impl_compat_14_shmctl(                              \
      res, (long long)(shmid), (long long)(cmd), (long long)(buf))
#define __sanitizer_syscall_pre_shmdt(shmaddr)                                 \
  __sanitizer_syscall_pre_impl_shmdt((long long)(shmaddr))
#define __sanitizer_syscall_post_shmdt(res, shmaddr)                           \
  __sanitizer_syscall_post_impl_shmdt(res, (long long)(shmaddr))
#define __sanitizer_syscall_pre_shmget(key, size, shmflg)                      \
  __sanitizer_syscall_pre_impl_shmget((long long)(key), (long long)(size),     \
                                      (long long)(shmflg))
#define __sanitizer_syscall_post_shmget(res, key, size, shmflg)                \
  __sanitizer_syscall_post_impl_shmget(res, (long long)(key),                  \
                                       (long long)(size), (long long)(shmflg))
#define __sanitizer_syscall_pre_compat_50_clock_gettime(clock_id, tp)          \
  __sanitizer_syscall_pre_impl_compat_50_clock_gettime((long long)(clock_id),  \
                                                       (long long)(tp))
#define __sanitizer_syscall_post_compat_50_clock_gettime(res, clock_id, tp)    \
  __sanitizer_syscall_post_impl_compat_50_clock_gettime(                       \
      res, (long long)(clock_id), (long long)(tp))
#define __sanitizer_syscall_pre_compat_50_clock_settime(clock_id, tp)          \
  __sanitizer_syscall_pre_impl_compat_50_clock_settime((long long)(clock_id),  \
                                                       (long long)(tp))
#define __sanitizer_syscall_post_compat_50_clock_settime(res, clock_id, tp)    \
  __sanitizer_syscall_post_impl_compat_50_clock_settime(                       \
      res, (long long)(clock_id), (long long)(tp))
#define __sanitizer_syscall_pre_compat_50_clock_getres(clock_id, tp)           \
  __sanitizer_syscall_pre_impl_compat_50_clock_getres((long long)(clock_id),   \
                                                      (long long)(tp))
#define __sanitizer_syscall_post_compat_50_clock_getres(res, clock_id, tp)     \
  __sanitizer_syscall_post_impl_compat_50_clock_getres(                        \
      res, (long long)(clock_id), (long long)(tp))
#define __sanitizer_syscall_pre_timer_create(clock_id, evp, timerid)           \
  __sanitizer_syscall_pre_impl_timer_create(                                   \
      (long long)(clock_id), (long long)(evp), (long long)(timerid))
#define __sanitizer_syscall_post_timer_create(res, clock_id, evp, timerid)     \
  __sanitizer_syscall_post_impl_timer_create(                                  \
      res, (long long)(clock_id), (long long)(evp), (long long)(timerid))
#define __sanitizer_syscall_pre_timer_delete(timerid)                          \
  __sanitizer_syscall_pre_impl_timer_delete((long long)(timerid))
#define __sanitizer_syscall_post_timer_delete(res, timerid)                    \
  __sanitizer_syscall_post_impl_timer_delete(res, (long long)(timerid))
#define __sanitizer_syscall_pre_compat_50_timer_settime(timerid, flags, value, \
                                                        ovalue)                \
  __sanitizer_syscall_pre_impl_compat_50_timer_settime(                        \
      (long long)(timerid), (long long)(flags), (long long)(value),            \
      (long long)(ovalue))
#define __sanitizer_syscall_post_compat_50_timer_settime(res, timerid, flags,  \
                                                         value, ovalue)        \
  __sanitizer_syscall_post_impl_compat_50_timer_settime(                       \
      res, (long long)(timerid), (long long)(flags), (long long)(value),       \
      (long long)(ovalue))
#define __sanitizer_syscall_pre_compat_50_timer_gettime(timerid, value)        \
  __sanitizer_syscall_pre_impl_compat_50_timer_gettime((long long)(timerid),   \
                                                       (long long)(value))
#define __sanitizer_syscall_post_compat_50_timer_gettime(res, timerid, value)  \
  __sanitizer_syscall_post_impl_compat_50_timer_gettime(                       \
      res, (long long)(timerid), (long long)(value))
#define __sanitizer_syscall_pre_timer_getoverrun(timerid)                      \
  __sanitizer_syscall_pre_impl_timer_getoverrun((long long)(timerid))
#define __sanitizer_syscall_post_timer_getoverrun(res, timerid)                \
  __sanitizer_syscall_post_impl_timer_getoverrun(res, (long long)(timerid))
#define __sanitizer_syscall_pre_compat_50_nanosleep(rqtp, rmtp)                \
  __sanitizer_syscall_pre_impl_compat_50_nanosleep((long long)(rqtp),          \
                                                   (long long)(rmtp))
#define __sanitizer_syscall_post_compat_50_nanosleep(res, rqtp, rmtp)          \
  __sanitizer_syscall_post_impl_compat_50_nanosleep(res, (long long)(rqtp),    \
                                                    (long long)(rmtp))
#define __sanitizer_syscall_pre_fdatasync(fd)                                  \
  __sanitizer_syscall_pre_impl_fdatasync((long long)(fd))
#define __sanitizer_syscall_post_fdatasync(res, fd)                            \
  __sanitizer_syscall_post_impl_fdatasync(res, (long long)(fd))
#define __sanitizer_syscall_pre_mlockall(flags)                                \
  __sanitizer_syscall_pre_impl_mlockall((long long)(flags))
#define __sanitizer_syscall_post_mlockall(res, flags)                          \
  __sanitizer_syscall_post_impl_mlockall(res, (long long)(flags))
#define __sanitizer_syscall_pre_munlockall()                                   \
  __sanitizer_syscall_pre_impl_munlockall()
#define __sanitizer_syscall_post_munlockall(res)                               \
  __sanitizer_syscall_post_impl_munlockall(res)
#define __sanitizer_syscall_pre_compat_50___sigtimedwait(set, info, timeout)   \
  __sanitizer_syscall_pre_impl_compat_50___sigtimedwait(                       \
      (long long)(set), (long long)(info), (long long)(timeout))
#define __sanitizer_syscall_post_compat_50___sigtimedwait(res, set, info,      \
                                                          timeout)             \
  __sanitizer_syscall_post_impl_compat_50___sigtimedwait(                      \
      res, (long long)(set), (long long)(info), (long long)(timeout))
#define __sanitizer_syscall_pre_sigqueueinfo(pid, info)                        \
  __sanitizer_syscall_pre_impl_sigqueueinfo((long long)(pid), (long long)(info))
#define __sanitizer_syscall_post_sigqueueinfo(res, pid, info)                  \
  __sanitizer_syscall_post_impl_sigqueueinfo(res, (long long)(pid),            \
                                             (long long)(info))
#define __sanitizer_syscall_pre_modctl(cmd, arg)                               \
  __sanitizer_syscall_pre_impl_modctl((long long)(cmd), (long long)(arg))
#define __sanitizer_syscall_post_modctl(res, cmd, arg)                         \
  __sanitizer_syscall_post_impl_modctl(res, (long long)(cmd), (long long)(arg))
#define __sanitizer_syscall_pre__ksem_init(value, idp)                         \
  __sanitizer_syscall_pre_impl__ksem_init((long long)(value), (long long)(idp))
#define __sanitizer_syscall_post__ksem_init(res, value, idp)                   \
  __sanitizer_syscall_post_impl__ksem_init(res, (long long)(value),            \
                                           (long long)(idp))
#define __sanitizer_syscall_pre__ksem_open(name, oflag, mode, value, idp)      \
  __sanitizer_syscall_pre_impl__ksem_open(                                     \
      (long long)(name), (long long)(oflag), (long long)(mode),                \
      (long long)(value), (long long)(idp))
#define __sanitizer_syscall_post__ksem_open(res, name, oflag, mode, value,     \
                                            idp)                               \
  __sanitizer_syscall_post_impl__ksem_open(                                    \
      res, (long long)(name), (long long)(oflag), (long long)(mode),           \
      (long long)(value), (long long)(idp))
#define __sanitizer_syscall_pre__ksem_unlink(name)                             \
  __sanitizer_syscall_pre_impl__ksem_unlink((long long)(name))
#define __sanitizer_syscall_post__ksem_unlink(res, name)                       \
  __sanitizer_syscall_post_impl__ksem_unlink(res, (long long)(name))
#define __sanitizer_syscall_pre__ksem_close(id)                                \
  __sanitizer_syscall_pre_impl__ksem_close((long long)(id))
#define __sanitizer_syscall_post__ksem_close(res, id)                          \
  __sanitizer_syscall_post_impl__ksem_close(res, (long long)(id))
#define __sanitizer_syscall_pre__ksem_post(id)                                 \
  __sanitizer_syscall_pre_impl__ksem_post((long long)(id))
#define __sanitizer_syscall_post__ksem_post(res, id)                           \
  __sanitizer_syscall_post_impl__ksem_post(res, (long long)(id))
#define __sanitizer_syscall_pre__ksem_wait(id)                                 \
  __sanitizer_syscall_pre_impl__ksem_wait((long long)(id))
#define __sanitizer_syscall_post__ksem_wait(res, id)                           \
  __sanitizer_syscall_post_impl__ksem_wait(res, (long long)(id))
#define __sanitizer_syscall_pre__ksem_trywait(id)                              \
  __sanitizer_syscall_pre_impl__ksem_trywait((long long)(id))
#define __sanitizer_syscall_post__ksem_trywait(res, id)                        \
  __sanitizer_syscall_post_impl__ksem_trywait(res, (long long)(id))
#define __sanitizer_syscall_pre__ksem_getvalue(id, value)                      \
  __sanitizer_syscall_pre_impl__ksem_getvalue((long long)(id),                 \
                                              (long long)(value))
#define __sanitizer_syscall_post__ksem_getvalue(res, id, value)                \
  __sanitizer_syscall_post_impl__ksem_getvalue(res, (long long)(id),           \
                                               (long long)(value))
#define __sanitizer_syscall_pre__ksem_destroy(id)                              \
  __sanitizer_syscall_pre_impl__ksem_destroy((long long)(id))
#define __sanitizer_syscall_post__ksem_destroy(res, id)                        \
  __sanitizer_syscall_post_impl__ksem_destroy(res, (long long)(id))
#define __sanitizer_syscall_pre__ksem_timedwait(id, abstime)                   \
  __sanitizer_syscall_pre_impl__ksem_timedwait((long long)(id),                \
                                               (long long)(abstime))
#define __sanitizer_syscall_post__ksem_timedwait(res, id, abstime)             \
  __sanitizer_syscall_post_impl__ksem_timedwait(res, (long long)(id),          \
                                                (long long)(abstime))
#define __sanitizer_syscall_pre_mq_open(name, oflag, mode, attr)               \
  __sanitizer_syscall_pre_impl_mq_open((long long)(name), (long long)(oflag),  \
                                       (long long)(mode), (long long)(attr))
#define __sanitizer_syscall_post_mq_open(res, name, oflag, mode, attr)         \
  __sanitizer_syscall_post_impl_mq_open(res, (long long)(name),                \
                                        (long long)(oflag), (long long)(mode), \
                                        (long long)(attr))
#define __sanitizer_syscall_pre_mq_close(mqdes)                                \
  __sanitizer_syscall_pre_impl_mq_close((long long)(mqdes))
#define __sanitizer_syscall_post_mq_close(res, mqdes)                          \
  __sanitizer_syscall_post_impl_mq_close(res, (long long)(mqdes))
#define __sanitizer_syscall_pre_mq_unlink(name)                                \
  __sanitizer_syscall_pre_impl_mq_unlink((long long)(name))
#define __sanitizer_syscall_post_mq_unlink(res, name)                          \
  __sanitizer_syscall_post_impl_mq_unlink(res, (long long)(name))
#define __sanitizer_syscall_pre_mq_getattr(mqdes, mqstat)                      \
  __sanitizer_syscall_pre_impl_mq_getattr((long long)(mqdes),                  \
                                          (long long)(mqstat))
#define __sanitizer_syscall_post_mq_getattr(res, mqdes, mqstat)                \
  __sanitizer_syscall_post_impl_mq_getattr(res, (long long)(mqdes),            \
                                           (long long)(mqstat))
#define __sanitizer_syscall_pre_mq_setattr(mqdes, mqstat, omqstat)             \
  __sanitizer_syscall_pre_impl_mq_setattr(                                     \
      (long long)(mqdes), (long long)(mqstat), (long long)(omqstat))
#define __sanitizer_syscall_post_mq_setattr(res, mqdes, mqstat, omqstat)       \
  __sanitizer_syscall_post_impl_mq_setattr(                                    \
      res, (long long)(mqdes), (long long)(mqstat), (long long)(omqstat))
#define __sanitizer_syscall_pre_mq_notify(mqdes, notification)                 \
  __sanitizer_syscall_pre_impl_mq_notify((long long)(mqdes),                   \
                                         (long long)(notification))
#define __sanitizer_syscall_post_mq_notify(res, mqdes, notification)           \
  __sanitizer_syscall_post_impl_mq_notify(res, (long long)(mqdes),             \
                                          (long long)(notification))
#define __sanitizer_syscall_pre_mq_send(mqdes, msg_ptr, msg_len, msg_prio)     \
  __sanitizer_syscall_pre_impl_mq_send(                                        \
      (long long)(mqdes), (long long)(msg_ptr), (long long)(msg_len),          \
      (long long)(msg_prio))
#define __sanitizer_syscall_post_mq_send(res, mqdes, msg_ptr, msg_len,         \
                                         msg_prio)                             \
  __sanitizer_syscall_post_impl_mq_send(                                       \
      res, (long long)(mqdes), (long long)(msg_ptr), (long long)(msg_len),     \
      (long long)(msg_prio))
#define __sanitizer_syscall_pre_mq_receive(mqdes, msg_ptr, msg_len, msg_prio)  \
  __sanitizer_syscall_pre_impl_mq_receive(                                     \
      (long long)(mqdes), (long long)(msg_ptr), (long long)(msg_len),          \
      (long long)(msg_prio))
#define __sanitizer_syscall_post_mq_receive(res, mqdes, msg_ptr, msg_len,      \
                                            msg_prio)                          \
  __sanitizer_syscall_post_impl_mq_receive(                                    \
      res, (long long)(mqdes), (long long)(msg_ptr), (long long)(msg_len),     \
      (long long)(msg_prio))
#define __sanitizer_syscall_pre_compat_50_mq_timedsend(                        \
    mqdes, msg_ptr, msg_len, msg_prio, abs_timeout)                            \
  __sanitizer_syscall_pre_impl_compat_50_mq_timedsend(                         \
      (long long)(mqdes), (long long)(msg_ptr), (long long)(msg_len),          \
      (long long)(msg_prio), (long long)(abs_timeout))
#define __sanitizer_syscall_post_compat_50_mq_timedsend(                       \
    res, mqdes, msg_ptr, msg_len, msg_prio, abs_timeout)                       \
  __sanitizer_syscall_post_impl_compat_50_mq_timedsend(                        \
      res, (long long)(mqdes), (long long)(msg_ptr), (long long)(msg_len),     \
      (long long)(msg_prio), (long long)(abs_timeout))
#define __sanitizer_syscall_pre_compat_50_mq_timedreceive(                     \
    mqdes, msg_ptr, msg_len, msg_prio, abs_timeout)                            \
  __sanitizer_syscall_pre_impl_compat_50_mq_timedreceive(                      \
      (long long)(mqdes), (long long)(msg_ptr), (long long)(msg_len),          \
      (long long)(msg_prio), (long long)(abs_timeout))
#define __sanitizer_syscall_post_compat_50_mq_timedreceive(                    \
    res, mqdes, msg_ptr, msg_len, msg_prio, abs_timeout)                       \
  __sanitizer_syscall_post_impl_compat_50_mq_timedreceive(                     \
      res, (long long)(mqdes), (long long)(msg_ptr), (long long)(msg_len),     \
      (long long)(msg_prio), (long long)(abs_timeout))
/* syscall 267 has been skipped */
/* syscall 268 has been skipped */
/* syscall 269 has been skipped */
#define __sanitizer_syscall_pre___posix_rename(from, to)                       \
  __sanitizer_syscall_pre_impl___posix_rename((long long)(from),               \
                                              (long long)(to))
#define __sanitizer_syscall_post___posix_rename(res, from, to)                 \
  __sanitizer_syscall_post_impl___posix_rename(res, (long long)(from),         \
                                               (long long)(to))
#define __sanitizer_syscall_pre_swapctl(cmd, arg, misc)                        \
  __sanitizer_syscall_pre_impl_swapctl((long long)(cmd), (long long)(arg),     \
                                       (long long)(misc))
#define __sanitizer_syscall_post_swapctl(res, cmd, arg, misc)                  \
  __sanitizer_syscall_post_impl_swapctl(res, (long long)(cmd),                 \
                                        (long long)(arg), (long long)(misc))
#define __sanitizer_syscall_pre_compat_30_getdents(fd, buf, count)             \
  __sanitizer_syscall_pre_impl_compat_30_getdents(                             \
      (long long)(fd), (long long)(buf), (long long)(count))
#define __sanitizer_syscall_post_compat_30_getdents(res, fd, buf, count)       \
  __sanitizer_syscall_post_impl_compat_30_getdents(                            \
      res, (long long)(fd), (long long)(buf), (long long)(count))
#define __sanitizer_syscall_pre_minherit(addr, len, inherit)                   \
  __sanitizer_syscall_pre_impl_minherit((long long)(addr), (long long)(len),   \
                                        (long long)(inherit))
#define __sanitizer_syscall_post_minherit(res, addr, len, inherit)             \
  __sanitizer_syscall_post_impl_minherit(                                      \
      res, (long long)(addr), (long long)(len), (long long)(inherit))
#define __sanitizer_syscall_pre_lchmod(path, mode)                             \
  __sanitizer_syscall_pre_impl_lchmod((long long)(path), (long long)(mode))
#define __sanitizer_syscall_post_lchmod(res, path, mode)                       \
  __sanitizer_syscall_post_impl_lchmod(res, (long long)(path),                 \
                                       (long long)(mode))
#define __sanitizer_syscall_pre_lchown(path, uid, gid)                         \
  __sanitizer_syscall_pre_impl_lchown((long long)(path), (long long)(uid),     \
                                      (long long)(gid))
#define __sanitizer_syscall_post_lchown(res, path, uid, gid)                   \
  __sanitizer_syscall_post_impl_lchown(res, (long long)(path),                 \
                                       (long long)(uid), (long long)(gid))
#define __sanitizer_syscall_pre_compat_50_lutimes(path, tptr)                  \
  __sanitizer_syscall_pre_impl_compat_50_lutimes((long long)(path),            \
                                                 (long long)(tptr))
#define __sanitizer_syscall_post_compat_50_lutimes(res, path, tptr)            \
  __sanitizer_syscall_post_impl_compat_50_lutimes(res, (long long)(path),      \
                                                  (long long)(tptr))
#define __sanitizer_syscall_pre___msync13(addr, len, flags)                    \
  __sanitizer_syscall_pre_impl___msync13((long long)(addr), (long long)(len),  \
                                         (long long)(flags))
#define __sanitizer_syscall_post___msync13(res, addr, len, flags)              \
  __sanitizer_syscall_post_impl___msync13(                                     \
      res, (long long)(addr), (long long)(len), (long long)(flags))
#define __sanitizer_syscall_pre_compat_30___stat13(path, ub)                   \
  __sanitizer_syscall_pre_impl_compat_30___stat13((long long)(path),           \
                                                  (long long)(ub))
#define __sanitizer_syscall_post_compat_30___stat13(res, path, ub)             \
  __sanitizer_syscall_post_impl_compat_30___stat13(res, (long long)(path),     \
                                                   (long long)(ub))
#define __sanitizer_syscall_pre_compat_30___fstat13(fd, sb)                    \
  __sanitizer_syscall_pre_impl_compat_30___fstat13((long long)(fd),            \
                                                   (long long)(sb))
#define __sanitizer_syscall_post_compat_30___fstat13(res, fd, sb)              \
  __sanitizer_syscall_post_impl_compat_30___fstat13(res, (long long)(fd),      \
                                                    (long long)(sb))
#define __sanitizer_syscall_pre_compat_30___lstat13(path, ub)                  \
  __sanitizer_syscall_pre_impl_compat_30___lstat13((long long)(path),          \
                                                   (long long)(ub))
#define __sanitizer_syscall_post_compat_30___lstat13(res, path, ub)            \
  __sanitizer_syscall_post_impl_compat_30___lstat13(res, (long long)(path),    \
                                                    (long long)(ub))
#define __sanitizer_syscall_pre___sigaltstack14(nss, oss)                      \
  __sanitizer_syscall_pre_impl___sigaltstack14((long long)(nss),               \
                                               (long long)(oss))
#define __sanitizer_syscall_post___sigaltstack14(res, nss, oss)                \
  __sanitizer_syscall_post_impl___sigaltstack14(res, (long long)(nss),         \
                                                (long long)(oss))
#define __sanitizer_syscall_pre___vfork14()                                    \
  __sanitizer_syscall_pre_impl___vfork14()
#define __sanitizer_syscall_post___vfork14(res)                                \
  __sanitizer_syscall_post_impl___vfork14(res)
#define __sanitizer_syscall_pre___posix_chown(path, uid, gid)                  \
  __sanitizer_syscall_pre_impl___posix_chown(                                  \
      (long long)(path), (long long)(uid), (long long)(gid))
#define __sanitizer_syscall_post___posix_chown(res, path, uid, gid)            \
  __sanitizer_syscall_post_impl___posix_chown(                                 \
      res, (long long)(path), (long long)(uid), (long long)(gid))
#define __sanitizer_syscall_pre___posix_fchown(fd, uid, gid)                   \
  __sanitizer_syscall_pre_impl___posix_fchown(                                 \
      (long long)(fd), (long long)(uid), (long long)(gid))
#define __sanitizer_syscall_post___posix_fchown(res, fd, uid, gid)             \
  __sanitizer_syscall_post_impl___posix_fchown(                                \
      res, (long long)(fd), (long long)(uid), (long long)(gid))
#define __sanitizer_syscall_pre___posix_lchown(path, uid, gid)                 \
  __sanitizer_syscall_pre_impl___posix_lchown(                                 \
      (long long)(path), (long long)(uid), (long long)(gid))
#define __sanitizer_syscall_post___posix_lchown(res, path, uid, gid)           \
  __sanitizer_syscall_post_impl___posix_lchown(                                \
      res, (long long)(path), (long long)(uid), (long long)(gid))
#define __sanitizer_syscall_pre_getsid(pid)                                    \
  __sanitizer_syscall_pre_impl_getsid((long long)(pid))
#define __sanitizer_syscall_post_getsid(res, pid)                              \
  __sanitizer_syscall_post_impl_getsid(res, (long long)(pid))
#define __sanitizer_syscall_pre___clone(flags, stack)                          \
  __sanitizer_syscall_pre_impl___clone((long long)(flags), (long long)(stack))
#define __sanitizer_syscall_post___clone(res, flags, stack)                    \
  __sanitizer_syscall_post_impl___clone(res, (long long)(flags),               \
                                        (long long)(stack))
#define __sanitizer_syscall_pre_fktrace(fd, ops, facs, pid)                    \
  __sanitizer_syscall_pre_impl_fktrace((long long)(fd), (long long)(ops),      \
                                       (long long)(facs), (long long)(pid))
#define __sanitizer_syscall_post_fktrace(res, fd, ops, facs, pid)              \
  __sanitizer_syscall_post_impl_fktrace(res, (long long)(fd),                  \
                                        (long long)(ops), (long long)(facs),   \
                                        (long long)(pid))
#define __sanitizer_syscall_pre_preadv(fd, iovp, iovcnt, PAD, offset)          \
  __sanitizer_syscall_pre_impl_preadv((long long)(fd), (long long)(iovp),      \
                                      (long long)(iovcnt), (long long)(PAD),   \
                                      (long long)(offset))
#define __sanitizer_syscall_post_preadv(res, fd, iovp, iovcnt, PAD, offset)    \
  __sanitizer_syscall_post_impl_preadv(res, (long long)(fd),                   \
                                       (long long)(iovp), (long long)(iovcnt), \
                                       (long long)(PAD), (long long)(offset))
#define __sanitizer_syscall_pre_pwritev(fd, iovp, iovcnt, PAD, offset)         \
  __sanitizer_syscall_pre_impl_pwritev((long long)(fd), (long long)(iovp),     \
                                       (long long)(iovcnt), (long long)(PAD),  \
                                       (long long)(offset))
#define __sanitizer_syscall_post_pwritev(res, fd, iovp, iovcnt, PAD, offset)   \
  __sanitizer_syscall_post_impl_pwritev(                                       \
      res, (long long)(fd), (long long)(iovp), (long long)(iovcnt),            \
      (long long)(PAD), (long long)(offset))
#define __sanitizer_syscall_pre_compat_16___sigaction14(signum, nsa, osa)      \
  __sanitizer_syscall_pre_impl_compat_16___sigaction14(                        \
      (long long)(signum), (long long)(nsa), (long long)(osa))
#define __sanitizer_syscall_post_compat_16___sigaction14(res, signum, nsa,     \
                                                         osa)                  \
  __sanitizer_syscall_post_impl_compat_16___sigaction14(                       \
      res, (long long)(signum), (long long)(nsa), (long long)(osa))
#define __sanitizer_syscall_pre___sigpending14(set)                            \
  __sanitizer_syscall_pre_impl___sigpending14((long long)(set))
#define __sanitizer_syscall_post___sigpending14(res, set)                      \
  __sanitizer_syscall_post_impl___sigpending14(res, (long long)(set))
#define __sanitizer_syscall_pre___sigprocmask14(how, set, oset)                \
  __sanitizer_syscall_pre_impl___sigprocmask14(                                \
      (long long)(how), (long long)(set), (long long)(oset))
#define __sanitizer_syscall_post___sigprocmask14(res, how, set, oset)          \
  __sanitizer_syscall_post_impl___sigprocmask14(                               \
      res, (long long)(how), (long long)(set), (long long)(oset))
#define __sanitizer_syscall_pre___sigsuspend14(set)                            \
  __sanitizer_syscall_pre_impl___sigsuspend14((long long)(set))
#define __sanitizer_syscall_post___sigsuspend14(res, set)                      \
  __sanitizer_syscall_post_impl___sigsuspend14(res, (long long)(set))
#define __sanitizer_syscall_pre_compat_16___sigreturn14(sigcntxp)              \
  __sanitizer_syscall_pre_impl_compat_16___sigreturn14((long long)(sigcntxp))
#define __sanitizer_syscall_post_compat_16___sigreturn14(res, sigcntxp)        \
  __sanitizer_syscall_post_impl_compat_16___sigreturn14(res,                   \
                                                        (long long)(sigcntxp))
#define __sanitizer_syscall_pre___getcwd(bufp, length)                         \
  __sanitizer_syscall_pre_impl___getcwd((long long)(bufp), (long long)(length))
#define __sanitizer_syscall_post___getcwd(res, bufp, length)                   \
  __sanitizer_syscall_post_impl___getcwd(res, (long long)(bufp),               \
                                         (long long)(length))
#define __sanitizer_syscall_pre_fchroot(fd)                                    \
  __sanitizer_syscall_pre_impl_fchroot((long long)(fd))
#define __sanitizer_syscall_post_fchroot(res, fd)                              \
  __sanitizer_syscall_post_impl_fchroot(res, (long long)(fd))
#define __sanitizer_syscall_pre_compat_30_fhopen(fhp, flags)                   \
  __sanitizer_syscall_pre_impl_compat_30_fhopen((long long)(fhp),              \
                                                (long long)(flags))
#define __sanitizer_syscall_post_compat_30_fhopen(res, fhp, flags)             \
  __sanitizer_syscall_post_impl_compat_30_fhopen(res, (long long)(fhp),        \
                                                 (long long)(flags))
#define __sanitizer_syscall_pre_compat_30_fhstat(fhp, sb)                      \
  __sanitizer_syscall_pre_impl_compat_30_fhstat((long long)(fhp),              \
                                                (long long)(sb))
#define __sanitizer_syscall_post_compat_30_fhstat(res, fhp, sb)                \
  __sanitizer_syscall_post_impl_compat_30_fhstat(res, (long long)(fhp),        \
                                                 (long long)(sb))
#define __sanitizer_syscall_pre_compat_20_fhstatfs(fhp, buf)                   \
  __sanitizer_syscall_pre_impl_compat_20_fhstatfs((long long)(fhp),            \
                                                  (long long)(buf))
#define __sanitizer_syscall_post_compat_20_fhstatfs(res, fhp, buf)             \
  __sanitizer_syscall_post_impl_compat_20_fhstatfs(res, (long long)(fhp),      \
                                                   (long long)(buf))
#define __sanitizer_syscall_pre_compat_50_____semctl13(semid, semnum, cmd,     \
                                                       arg)                    \
  __sanitizer_syscall_pre_impl_compat_50_____semctl13(                         \
      (long long)(semid), (long long)(semnum), (long long)(cmd),               \
      (long long)(arg))
#define __sanitizer_syscall_post_compat_50_____semctl13(res, semid, semnum,    \
                                                        cmd, arg)              \
  __sanitizer_syscall_post_impl_compat_50_____semctl13(                        \
      res, (long long)(semid), (long long)(semnum), (long long)(cmd),          \
      (long long)(arg))
#define __sanitizer_syscall_pre_compat_50___msgctl13(msqid, cmd, buf)          \
  __sanitizer_syscall_pre_impl_compat_50___msgctl13(                           \
      (long long)(msqid), (long long)(cmd), (long long)(buf))
#define __sanitizer_syscall_post_compat_50___msgctl13(res, msqid, cmd, buf)    \
  __sanitizer_syscall_post_impl_compat_50___msgctl13(                          \
      res, (long long)(msqid), (long long)(cmd), (long long)(buf))
#define __sanitizer_syscall_pre_compat_50___shmctl13(shmid, cmd, buf)          \
  __sanitizer_syscall_pre_impl_compat_50___shmctl13(                           \
      (long long)(shmid), (long long)(cmd), (long long)(buf))
#define __sanitizer_syscall_post_compat_50___shmctl13(res, shmid, cmd, buf)    \
  __sanitizer_syscall_post_impl_compat_50___shmctl13(                          \
      res, (long long)(shmid), (long long)(cmd), (long long)(buf))
#define __sanitizer_syscall_pre_lchflags(path, flags)                          \
  __sanitizer_syscall_pre_impl_lchflags((long long)(path), (long long)(flags))
#define __sanitizer_syscall_post_lchflags(res, path, flags)                    \
  __sanitizer_syscall_post_impl_lchflags(res, (long long)(path),               \
                                         (long long)(flags))
#define __sanitizer_syscall_pre_issetugid()                                    \
  __sanitizer_syscall_pre_impl_issetugid()
#define __sanitizer_syscall_post_issetugid(res)                                \
  __sanitizer_syscall_post_impl_issetugid(res)
#define __sanitizer_syscall_pre_utrace(label, addr, len)                       \
  __sanitizer_syscall_pre_impl_utrace((long long)(label), (long long)(addr),   \
                                      (long long)(len))
#define __sanitizer_syscall_post_utrace(res, label, addr, len)                 \
  __sanitizer_syscall_post_impl_utrace(res, (long long)(label),                \
                                       (long long)(addr), (long long)(len))
#define __sanitizer_syscall_pre_getcontext(ucp)                                \
  __sanitizer_syscall_pre_impl_getcontext((long long)(ucp))
#define __sanitizer_syscall_post_getcontext(res, ucp)                          \
  __sanitizer_syscall_post_impl_getcontext(res, (long long)(ucp))
#define __sanitizer_syscall_pre_setcontext(ucp)                                \
  __sanitizer_syscall_pre_impl_setcontext((long long)(ucp))
#define __sanitizer_syscall_post_setcontext(res, ucp)                          \
  __sanitizer_syscall_post_impl_setcontext(res, (long long)(ucp))
#define __sanitizer_syscall_pre__lwp_create(ucp, flags, new_lwp)               \
  __sanitizer_syscall_pre_impl__lwp_create(                                    \
      (long long)(ucp), (long long)(flags), (long long)(new_lwp))
#define __sanitizer_syscall_post__lwp_create(res, ucp, flags, new_lwp)         \
  __sanitizer_syscall_post_impl__lwp_create(                                   \
      res, (long long)(ucp), (long long)(flags), (long long)(new_lwp))
#define __sanitizer_syscall_pre__lwp_exit()                                    \
  __sanitizer_syscall_pre_impl__lwp_exit()
#define __sanitizer_syscall_post__lwp_exit(res)                                \
  __sanitizer_syscall_post_impl__lwp_exit(res)
#define __sanitizer_syscall_pre__lwp_self()                                    \
  __sanitizer_syscall_pre_impl__lwp_self()
#define __sanitizer_syscall_post__lwp_self(res)                                \
  __sanitizer_syscall_post_impl__lwp_self(res)
#define __sanitizer_syscall_pre__lwp_wait(wait_for, departed)                  \
  __sanitizer_syscall_pre_impl__lwp_wait((long long)(wait_for),                \
                                         (long long)(departed))
#define __sanitizer_syscall_post__lwp_wait(res, wait_for, departed)            \
  __sanitizer_syscall_post_impl__lwp_wait(res, (long long)(wait_for),          \
                                          (long long)(departed))
#define __sanitizer_syscall_pre__lwp_suspend(target)                           \
  __sanitizer_syscall_pre_impl__lwp_suspend((long long)(target))
#define __sanitizer_syscall_post__lwp_suspend(res, target)                     \
  __sanitizer_syscall_post_impl__lwp_suspend(res, (long long)(target))
#define __sanitizer_syscall_pre__lwp_continue(target)                          \
  __sanitizer_syscall_pre_impl__lwp_continue((long long)(target))
#define __sanitizer_syscall_post__lwp_continue(res, target)                    \
  __sanitizer_syscall_post_impl__lwp_continue(res, (long long)(target))
#define __sanitizer_syscall_pre__lwp_wakeup(target)                            \
  __sanitizer_syscall_pre_impl__lwp_wakeup((long long)(target))
#define __sanitizer_syscall_post__lwp_wakeup(res, target)                      \
  __sanitizer_syscall_post_impl__lwp_wakeup(res, (long long)(target))
#define __sanitizer_syscall_pre__lwp_getprivate()                              \
  __sanitizer_syscall_pre_impl__lwp_getprivate()
#define __sanitizer_syscall_post__lwp_getprivate(res)                          \
  __sanitizer_syscall_post_impl__lwp_getprivate(res)
#define __sanitizer_syscall_pre__lwp_setprivate(ptr)                           \
  __sanitizer_syscall_pre_impl__lwp_setprivate((long long)(ptr))
#define __sanitizer_syscall_post__lwp_setprivate(res, ptr)                     \
  __sanitizer_syscall_post_impl__lwp_setprivate(res, (long long)(ptr))
#define __sanitizer_syscall_pre__lwp_kill(target, signo)                       \
  __sanitizer_syscall_pre_impl__lwp_kill((long long)(target),                  \
                                         (long long)(signo))
#define __sanitizer_syscall_post__lwp_kill(res, target, signo)                 \
  __sanitizer_syscall_post_impl__lwp_kill(res, (long long)(target),            \
                                          (long long)(signo))
#define __sanitizer_syscall_pre__lwp_detach(target)                            \
  __sanitizer_syscall_pre_impl__lwp_detach((long long)(target))
#define __sanitizer_syscall_post__lwp_detach(res, target)                      \
  __sanitizer_syscall_post_impl__lwp_detach(res, (long long)(target))
#define __sanitizer_syscall_pre_compat_50__lwp_park(ts, unpark, hint,          \
                                                    unparkhint)                \
  __sanitizer_syscall_pre_impl_compat_50__lwp_park(                            \
      (long long)(ts), (long long)(unpark), (long long)(hint),                 \
      (long long)(unparkhint))
#define __sanitizer_syscall_post_compat_50__lwp_park(res, ts, unpark, hint,    \
                                                     unparkhint)               \
  __sanitizer_syscall_post_impl_compat_50__lwp_park(                           \
      res, (long long)(ts), (long long)(unpark), (long long)(hint),            \
      (long long)(unparkhint))
#define __sanitizer_syscall_pre__lwp_unpark(target, hint)                      \
  __sanitizer_syscall_pre_impl__lwp_unpark((long long)(target),                \
                                           (long long)(hint))
#define __sanitizer_syscall_post__lwp_unpark(res, target, hint)                \
  __sanitizer_syscall_post_impl__lwp_unpark(res, (long long)(target),          \
                                            (long long)(hint))
#define __sanitizer_syscall_pre__lwp_unpark_all(targets, ntargets, hint)       \
  __sanitizer_syscall_pre_impl__lwp_unpark_all(                                \
      (long long)(targets), (long long)(ntargets), (long long)(hint))
#define __sanitizer_syscall_post__lwp_unpark_all(res, targets, ntargets, hint) \
  __sanitizer_syscall_post_impl__lwp_unpark_all(                               \
      res, (long long)(targets), (long long)(ntargets), (long long)(hint))
#define __sanitizer_syscall_pre__lwp_setname(target, name)                     \
  __sanitizer_syscall_pre_impl__lwp_setname((long long)(target),               \
                                            (long long)(name))
#define __sanitizer_syscall_post__lwp_setname(res, target, name)               \
  __sanitizer_syscall_post_impl__lwp_setname(res, (long long)(target),         \
                                             (long long)(name))
#define __sanitizer_syscall_pre__lwp_getname(target, name, len)                \
  __sanitizer_syscall_pre_impl__lwp_getname(                                   \
      (long long)(target), (long long)(name), (long long)(len))
#define __sanitizer_syscall_post__lwp_getname(res, target, name, len)          \
  __sanitizer_syscall_post_impl__lwp_getname(                                  \
      res, (long long)(target), (long long)(name), (long long)(len))
#define __sanitizer_syscall_pre__lwp_ctl(features, address)                    \
  __sanitizer_syscall_pre_impl__lwp_ctl((long long)(features),                 \
                                        (long long)(address))
#define __sanitizer_syscall_post__lwp_ctl(res, features, address)              \
  __sanitizer_syscall_post_impl__lwp_ctl(res, (long long)(features),           \
                                         (long long)(address))
/* syscall 326 has been skipped */
/* syscall 327 has been skipped */
/* syscall 328 has been skipped */
/* syscall 329 has been skipped */
#define __sanitizer_syscall_pre_compat_60_sa_register(newv, oldv, flags,       \
                                                      stackinfo_offset)        \
  __sanitizer_syscall_pre_impl_compat_60_sa_register(                          \
      (long long)(newv), (long long)(oldv), (long long)(flags),                \
      (long long)(stackinfo_offset))
#define __sanitizer_syscall_post_compat_60_sa_register(res, newv, oldv, flags, \
                                                       stackinfo_offset)       \
  __sanitizer_syscall_post_impl_compat_60_sa_register(                         \
      res, (long long)(newv), (long long)(oldv), (long long)(flags),           \
      (long long)(stackinfo_offset))
#define __sanitizer_syscall_pre_compat_60_sa_stacks(num, stacks)               \
  __sanitizer_syscall_pre_impl_compat_60_sa_stacks((long long)(num),           \
                                                   (long long)(stacks))
#define __sanitizer_syscall_post_compat_60_sa_stacks(res, num, stacks)         \
  __sanitizer_syscall_post_impl_compat_60_sa_stacks(res, (long long)(num),     \
                                                    (long long)(stacks))
#define __sanitizer_syscall_pre_compat_60_sa_enable()                          \
  __sanitizer_syscall_pre_impl_compat_60_sa_enable()
#define __sanitizer_syscall_post_compat_60_sa_enable(res)                      \
  __sanitizer_syscall_post_impl_compat_60_sa_enable(res)
#define __sanitizer_syscall_pre_compat_60_sa_setconcurrency(concurrency)       \
  __sanitizer_syscall_pre_impl_compat_60_sa_setconcurrency(                    \
      (long long)(concurrency))
#define __sanitizer_syscall_post_compat_60_sa_setconcurrency(res, concurrency) \
  __sanitizer_syscall_post_impl_compat_60_sa_setconcurrency(                   \
      res, (long long)(concurrency))
#define __sanitizer_syscall_pre_compat_60_sa_yield()                           \
  __sanitizer_syscall_pre_impl_compat_60_sa_yield()
#define __sanitizer_syscall_post_compat_60_sa_yield(res)                       \
  __sanitizer_syscall_post_impl_compat_60_sa_yield(res)
#define __sanitizer_syscall_pre_compat_60_sa_preempt(sa_id)                    \
  __sanitizer_syscall_pre_impl_compat_60_sa_preempt((long long)(sa_id))
#define __sanitizer_syscall_post_compat_60_sa_preempt(res, sa_id)              \
  __sanitizer_syscall_post_impl_compat_60_sa_preempt(res, (long long)(sa_id))
/* syscall 336 has been skipped */
/* syscall 337 has been skipped */
/* syscall 338 has been skipped */
/* syscall 339 has been skipped */
#define __sanitizer_syscall_pre___sigaction_sigtramp(signum, nsa, osa, tramp,  \
                                                     vers)                     \
  __sanitizer_syscall_pre_impl___sigaction_sigtramp(                           \
      (long long)(signum), (long long)(nsa), (long long)(osa),                 \
      (long long)(tramp), (long long)(vers))
#define __sanitizer_syscall_post___sigaction_sigtramp(res, signum, nsa, osa,   \
                                                      tramp, vers)             \
  __sanitizer_syscall_post_impl___sigaction_sigtramp(                          \
      res, (long long)(signum), (long long)(nsa), (long long)(osa),            \
      (long long)(tramp), (long long)(vers))
/* syscall 341 has been skipped */
/* syscall 342 has been skipped */
#define __sanitizer_syscall_pre_rasctl(addr, len, op)                          \
  __sanitizer_syscall_pre_impl_rasctl((long long)(addr), (long long)(len),     \
                                      (long long)(op))
#define __sanitizer_syscall_post_rasctl(res, addr, len, op)                    \
  __sanitizer_syscall_post_impl_rasctl(res, (long long)(addr),                 \
                                       (long long)(len), (long long)(op))
#define __sanitizer_syscall_pre_kqueue() __sanitizer_syscall_pre_impl_kqueue()
#define __sanitizer_syscall_post_kqueue(res)                                   \
  __sanitizer_syscall_post_impl_kqueue(res)
#define __sanitizer_syscall_pre_compat_50_kevent(fd, changelist, nchanges,     \
                                                 eventlist, nevents, timeout)  \
  __sanitizer_syscall_pre_impl_compat_50_kevent(                               \
      (long long)(fd), (long long)(changelist), (long long)(nchanges),         \
      (long long)(eventlist), (long long)(nevents), (long long)(timeout))
#define __sanitizer_syscall_post_compat_50_kevent(                             \
    res, fd, changelist, nchanges, eventlist, nevents, timeout)                \
  __sanitizer_syscall_post_impl_compat_50_kevent(                              \
      res, (long long)(fd), (long long)(changelist), (long long)(nchanges),    \
      (long long)(eventlist), (long long)(nevents), (long long)(timeout))
#define __sanitizer_syscall_pre__sched_setparam(pid, lid, policy, params)      \
  __sanitizer_syscall_pre_impl__sched_setparam(                                \
      (long long)(pid), (long long)(lid), (long long)(policy),                 \
      (long long)(params))
#define __sanitizer_syscall_post__sched_setparam(res, pid, lid, policy,        \
                                                 params)                       \
  __sanitizer_syscall_post_impl__sched_setparam(                               \
      res, (long long)(pid), (long long)(lid), (long long)(policy),            \
      (long long)(params))
#define __sanitizer_syscall_pre__sched_getparam(pid, lid, policy, params)      \
  __sanitizer_syscall_pre_impl__sched_getparam(                                \
      (long long)(pid), (long long)(lid), (long long)(policy),                 \
      (long long)(params))
#define __sanitizer_syscall_post__sched_getparam(res, pid, lid, policy,        \
                                                 params)                       \
  __sanitizer_syscall_post_impl__sched_getparam(                               \
      res, (long long)(pid), (long long)(lid), (long long)(policy),            \
      (long long)(params))
#define __sanitizer_syscall_pre__sched_setaffinity(pid, lid, size, cpuset)     \
  __sanitizer_syscall_pre_impl__sched_setaffinity(                             \
      (long long)(pid), (long long)(lid), (long long)(size),                   \
      (long long)(cpuset))
#define __sanitizer_syscall_post__sched_setaffinity(res, pid, lid, size,       \
                                                    cpuset)                    \
  __sanitizer_syscall_post_impl__sched_setaffinity(                            \
      res, (long long)(pid), (long long)(lid), (long long)(size),              \
      (long long)(cpuset))
#define __sanitizer_syscall_pre__sched_getaffinity(pid, lid, size, cpuset)     \
  __sanitizer_syscall_pre_impl__sched_getaffinity(                             \
      (long long)(pid), (long long)(lid), (long long)(size),                   \
      (long long)(cpuset))
#define __sanitizer_syscall_post__sched_getaffinity(res, pid, lid, size,       \
                                                    cpuset)                    \
  __sanitizer_syscall_post_impl__sched_getaffinity(                            \
      res, (long long)(pid), (long long)(lid), (long long)(size),              \
      (long long)(cpuset))
#define __sanitizer_syscall_pre_sched_yield()                                  \
  __sanitizer_syscall_pre_impl_sched_yield()
#define __sanitizer_syscall_post_sched_yield(res)                              \
  __sanitizer_syscall_post_impl_sched_yield(res)
#define __sanitizer_syscall_pre__sched_protect(priority)                       \
  __sanitizer_syscall_pre_impl__sched_protect((long long)(priority))
#define __sanitizer_syscall_post__sched_protect(res, priority)                 \
  __sanitizer_syscall_post_impl__sched_protect(res, (long long)(priority))
/* syscall 352 has been skipped */
/* syscall 353 has been skipped */
#define __sanitizer_syscall_pre_fsync_range(fd, flags, start, length)          \
  __sanitizer_syscall_pre_impl_fsync_range(                                    \
      (long long)(fd), (long long)(flags), (long long)(start),                 \
      (long long)(length))
#define __sanitizer_syscall_post_fsync_range(res, fd, flags, start, length)    \
  __sanitizer_syscall_post_impl_fsync_range(                                   \
      res, (long long)(fd), (long long)(flags), (long long)(start),            \
      (long long)(length))
#define __sanitizer_syscall_pre_uuidgen(store, count)                          \
  __sanitizer_syscall_pre_impl_uuidgen((long long)(store), (long long)(count))
#define __sanitizer_syscall_post_uuidgen(res, store, count)                    \
  __sanitizer_syscall_post_impl_uuidgen(res, (long long)(store),               \
                                        (long long)(count))
#define __sanitizer_syscall_pre_getvfsstat(buf, bufsize, flags)                \
  __sanitizer_syscall_pre_impl_getvfsstat(                                     \
      (long long)(buf), (long long)(bufsize), (long long)(flags))
#define __sanitizer_syscall_post_getvfsstat(res, buf, bufsize, flags)          \
  __sanitizer_syscall_post_impl_getvfsstat(                                    \
      res, (long long)(buf), (long long)(bufsize), (long long)(flags))
#define __sanitizer_syscall_pre_statvfs1(path, buf, flags)                     \
  __sanitizer_syscall_pre_impl_statvfs1((long long)(path), (long long)(buf),   \
                                        (long long)(flags))
#define __sanitizer_syscall_post_statvfs1(res, path, buf, flags)               \
  __sanitizer_syscall_post_impl_statvfs1(res, (long long)(path),               \
                                         (long long)(buf), (long long)(flags))
#define __sanitizer_syscall_pre_fstatvfs1(fd, buf, flags)                      \
  __sanitizer_syscall_pre_impl_fstatvfs1((long long)(fd), (long long)(buf),    \
                                         (long long)(flags))
#define __sanitizer_syscall_post_fstatvfs1(res, fd, buf, flags)                \
  __sanitizer_syscall_post_impl_fstatvfs1(                                     \
      res, (long long)(fd), (long long)(buf), (long long)(flags))
#define __sanitizer_syscall_pre_compat_30_fhstatvfs1(fhp, buf, flags)          \
  __sanitizer_syscall_pre_impl_compat_30_fhstatvfs1(                           \
      (long long)(fhp), (long long)(buf), (long long)(flags))
#define __sanitizer_syscall_post_compat_30_fhstatvfs1(res, fhp, buf, flags)    \
  __sanitizer_syscall_post_impl_compat_30_fhstatvfs1(                          \
      res, (long long)(fhp), (long long)(buf), (long long)(flags))
#define __sanitizer_syscall_pre_extattrctl(path, cmd, filename, attrnamespace, \
                                           attrname)                           \
  __sanitizer_syscall_pre_impl_extattrctl(                                     \
      (long long)(path), (long long)(cmd), (long long)(filename),              \
      (long long)(attrnamespace), (long long)(attrname))
#define __sanitizer_syscall_post_extattrctl(res, path, cmd, filename,          \
                                            attrnamespace, attrname)           \
  __sanitizer_syscall_post_impl_extattrctl(                                    \
      res, (long long)(path), (long long)(cmd), (long long)(filename),         \
      (long long)(attrnamespace), (long long)(attrname))
#define __sanitizer_syscall_pre_extattr_set_file(path, attrnamespace,          \
                                                 attrname, data, nbytes)       \
  __sanitizer_syscall_pre_impl_extattr_set_file(                               \
      (long long)(path), (long long)(attrnamespace), (long long)(attrname),    \
      (long long)(data), (long long)(nbytes))
#define __sanitizer_syscall_post_extattr_set_file(res, path, attrnamespace,    \
                                                  attrname, data, nbytes)      \
  __sanitizer_syscall_post_impl_extattr_set_file(                              \
      res, (long long)(path), (long long)(attrnamespace),                      \
      (long long)(attrname), (long long)(data), (long long)(nbytes))
#define __sanitizer_syscall_pre_extattr_get_file(path, attrnamespace,          \
                                                 attrname, data, nbytes)       \
  __sanitizer_syscall_pre_impl_extattr_get_file(                               \
      (long long)(path), (long long)(attrnamespace), (long long)(attrname),    \
      (long long)(data), (long long)(nbytes))
#define __sanitizer_syscall_post_extattr_get_file(res, path, attrnamespace,    \
                                                  attrname, data, nbytes)      \
  __sanitizer_syscall_post_impl_extattr_get_file(                              \
      res, (long long)(path), (long long)(attrnamespace),                      \
      (long long)(attrname), (long long)(data), (long long)(nbytes))
#define __sanitizer_syscall_pre_extattr_delete_file(path, attrnamespace,       \
                                                    attrname)                  \
  __sanitizer_syscall_pre_impl_extattr_delete_file(                            \
      (long long)(path), (long long)(attrnamespace), (long long)(attrname))
#define __sanitizer_syscall_post_extattr_delete_file(res, path, attrnamespace, \
                                                     attrname)                 \
  __sanitizer_syscall_post_impl_extattr_delete_file(                           \
      res, (long long)(path), (long long)(attrnamespace),                      \
      (long long)(attrname))
#define __sanitizer_syscall_pre_extattr_set_fd(fd, attrnamespace, attrname,    \
                                               data, nbytes)                   \
  __sanitizer_syscall_pre_impl_extattr_set_fd(                                 \
      (long long)(fd), (long long)(attrnamespace), (long long)(attrname),      \
      (long long)(data), (long long)(nbytes))
#define __sanitizer_syscall_post_extattr_set_fd(res, fd, attrnamespace,        \
                                                attrname, data, nbytes)        \
  __sanitizer_syscall_post_impl_extattr_set_fd(                                \
      res, (long long)(fd), (long long)(attrnamespace), (long long)(attrname), \
      (long long)(data), (long long)(nbytes))
#define __sanitizer_syscall_pre_extattr_get_fd(fd, attrnamespace, attrname,    \
                                               data, nbytes)                   \
  __sanitizer_syscall_pre_impl_extattr_get_fd(                                 \
      (long long)(fd), (long long)(attrnamespace), (long long)(attrname),      \
      (long long)(data), (long long)(nbytes))
#define __sanitizer_syscall_post_extattr_get_fd(res, fd, attrnamespace,        \
                                                attrname, data, nbytes)        \
  __sanitizer_syscall_post_impl_extattr_get_fd(                                \
      res, (long long)(fd), (long long)(attrnamespace), (long long)(attrname), \
      (long long)(data), (long long)(nbytes))
#define __sanitizer_syscall_pre_extattr_delete_fd(fd, attrnamespace, attrname) \
  __sanitizer_syscall_pre_impl_extattr_delete_fd(                              \
      (long long)(fd), (long long)(attrnamespace), (long long)(attrname))
#define __sanitizer_syscall_post_extattr_delete_fd(res, fd, attrnamespace,     \
                                                   attrname)                   \
  __sanitizer_syscall_post_impl_extattr_delete_fd(                             \
      res, (long long)(fd), (long long)(attrnamespace), (long long)(attrname))
#define __sanitizer_syscall_pre_extattr_set_link(path, attrnamespace,          \
                                                 attrname, data, nbytes)       \
  __sanitizer_syscall_pre_impl_extattr_set_link(                               \
      (long long)(path), (long long)(attrnamespace), (long long)(attrname),    \
      (long long)(data), (long long)(nbytes))
#define __sanitizer_syscall_post_extattr_set_link(res, path, attrnamespace,    \
                                                  attrname, data, nbytes)      \
  __sanitizer_syscall_post_impl_extattr_set_link(                              \
      res, (long long)(path), (long long)(attrnamespace),                      \
      (long long)(attrname), (long long)(data), (long long)(nbytes))
#define __sanitizer_syscall_pre_extattr_get_link(path, attrnamespace,          \
                                                 attrname, data, nbytes)       \
  __sanitizer_syscall_pre_impl_extattr_get_link(                               \
      (long long)(path), (long long)(attrnamespace), (long long)(attrname),    \
      (long long)(data), (long long)(nbytes))
#define __sanitizer_syscall_post_extattr_get_link(res, path, attrnamespace,    \
                                                  attrname, data, nbytes)      \
  __sanitizer_syscall_post_impl_extattr_get_link(                              \
      res, (long long)(path), (long long)(attrnamespace),                      \
      (long long)(attrname), (long long)(data), (long long)(nbytes))
#define __sanitizer_syscall_pre_extattr_delete_link(path, attrnamespace,       \
                                                    attrname)                  \
  __sanitizer_syscall_pre_impl_extattr_delete_link(                            \
      (long long)(path), (long long)(attrnamespace), (long long)(attrname))
#define __sanitizer_syscall_post_extattr_delete_link(res, path, attrnamespace, \
                                                     attrname)                 \
  __sanitizer_syscall_post_impl_extattr_delete_link(                           \
      res, (long long)(path), (long long)(attrnamespace),                      \
      (long long)(attrname))
#define __sanitizer_syscall_pre_extattr_list_fd(fd, attrnamespace, data,       \
                                                nbytes)                        \
  __sanitizer_syscall_pre_impl_extattr_list_fd(                                \
      (long long)(fd), (long long)(attrnamespace), (long long)(data),          \
      (long long)(nbytes))
#define __sanitizer_syscall_post_extattr_list_fd(res, fd, attrnamespace, data, \
                                                 nbytes)                       \
  __sanitizer_syscall_post_impl_extattr_list_fd(                               \
      res, (long long)(fd), (long long)(attrnamespace), (long long)(data),     \
      (long long)(nbytes))
#define __sanitizer_syscall_pre_extattr_list_file(path, attrnamespace, data,   \
                                                  nbytes)                      \
  __sanitizer_syscall_pre_impl_extattr_list_file(                              \
      (long long)(path), (long long)(attrnamespace), (long long)(data),        \
      (long long)(nbytes))
#define __sanitizer_syscall_post_extattr_list_file(res, path, attrnamespace,   \
                                                   data, nbytes)               \
  __sanitizer_syscall_post_impl_extattr_list_file(                             \
      res, (long long)(path), (long long)(attrnamespace), (long long)(data),   \
      (long long)(nbytes))
#define __sanitizer_syscall_pre_extattr_list_link(path, attrnamespace, data,   \
                                                  nbytes)                      \
  __sanitizer_syscall_pre_impl_extattr_list_link(                              \
      (long long)(path), (long long)(attrnamespace), (long long)(data),        \
      (long long)(nbytes))
#define __sanitizer_syscall_post_extattr_list_link(res, path, attrnamespace,   \
                                                   data, nbytes)               \
  __sanitizer_syscall_post_impl_extattr_list_link(                             \
      res, (long long)(path), (long long)(attrnamespace), (long long)(data),   \
      (long long)(nbytes))
#define __sanitizer_syscall_pre_compat_50_pselect(nd, in, ou, ex, ts, mask)    \
  __sanitizer_syscall_pre_impl_compat_50_pselect(                              \
      (long long)(nd), (long long)(in), (long long)(ou), (long long)(ex),      \
      (long long)(ts), (long long)(mask))
#define __sanitizer_syscall_post_compat_50_pselect(res, nd, in, ou, ex, ts,    \
                                                   mask)                       \
  __sanitizer_syscall_post_impl_compat_50_pselect(                             \
      res, (long long)(nd), (long long)(in), (long long)(ou), (long long)(ex), \
      (long long)(ts), (long long)(mask))
#define __sanitizer_syscall_pre_compat_50_pollts(fds, nfds, ts, mask)          \
  __sanitizer_syscall_pre_impl_compat_50_pollts(                               \
      (long long)(fds), (long long)(nfds), (long long)(ts), (long long)(mask))
#define __sanitizer_syscall_post_compat_50_pollts(res, fds, nfds, ts, mask)    \
  __sanitizer_syscall_post_impl_compat_50_pollts(                              \
      res, (long long)(fds), (long long)(nfds), (long long)(ts),               \
      (long long)(mask))
#define __sanitizer_syscall_pre_setxattr(path, name, value, size, flags)       \
  __sanitizer_syscall_pre_impl_setxattr((long long)(path), (long long)(name),  \
                                        (long long)(value), (long long)(size), \
                                        (long long)(flags))
#define __sanitizer_syscall_post_setxattr(res, path, name, value, size, flags) \
  __sanitizer_syscall_post_impl_setxattr(                                      \
      res, (long long)(path), (long long)(name), (long long)(value),           \
      (long long)(size), (long long)(flags))
#define __sanitizer_syscall_pre_lsetxattr(path, name, value, size, flags)      \
  __sanitizer_syscall_pre_impl_lsetxattr(                                      \
      (long long)(path), (long long)(name), (long long)(value),                \
      (long long)(size), (long long)(flags))
#define __sanitizer_syscall_post_lsetxattr(res, path, name, value, size,       \
                                           flags)                              \
  __sanitizer_syscall_post_impl_lsetxattr(                                     \
      res, (long long)(path), (long long)(name), (long long)(value),           \
      (long long)(size), (long long)(flags))
#define __sanitizer_syscall_pre_fsetxattr(fd, name, value, size, flags)        \
  __sanitizer_syscall_pre_impl_fsetxattr(                                      \
      (long long)(fd), (long long)(name), (long long)(value),                  \
      (long long)(size), (long long)(flags))
#define __sanitizer_syscall_post_fsetxattr(res, fd, name, value, size, flags)  \
  __sanitizer_syscall_post_impl_fsetxattr(                                     \
      res, (long long)(fd), (long long)(name), (long long)(value),             \
      (long long)(size), (long long)(flags))
#define __sanitizer_syscall_pre_getxattr(path, name, value, size)              \
  __sanitizer_syscall_pre_impl_getxattr((long long)(path), (long long)(name),  \
                                        (long long)(value), (long long)(size))
#define __sanitizer_syscall_post_getxattr(res, path, name, value, size)        \
  __sanitizer_syscall_post_impl_getxattr(                                      \
      res, (long long)(path), (long long)(name), (long long)(value),           \
      (long long)(size))
#define __sanitizer_syscall_pre_lgetxattr(path, name, value, size)             \
  __sanitizer_syscall_pre_impl_lgetxattr((long long)(path), (long long)(name), \
                                         (long long)(value),                   \
                                         (long long)(size))
#define __sanitizer_syscall_post_lgetxattr(res, path, name, value, size)       \
  __sanitizer_syscall_post_impl_lgetxattr(                                     \
      res, (long long)(path), (long long)(name), (long long)(value),           \
      (long long)(size))
#define __sanitizer_syscall_pre_fgetxattr(fd, name, value, size)               \
  __sanitizer_syscall_pre_impl_fgetxattr((long long)(fd), (long long)(name),   \
                                         (long long)(value),                   \
                                         (long long)(size))
#define __sanitizer_syscall_post_fgetxattr(res, fd, name, value, size)         \
  __sanitizer_syscall_post_impl_fgetxattr(                                     \
      res, (long long)(fd), (long long)(name), (long long)(value),             \
      (long long)(size))
#define __sanitizer_syscall_pre_listxattr(path, list, size)                    \
  __sanitizer_syscall_pre_impl_listxattr((long long)(path), (long long)(list), \
                                         (long long)(size))
#define __sanitizer_syscall_post_listxattr(res, path, list, size)              \
  __sanitizer_syscall_post_impl_listxattr(                                     \
      res, (long long)(path), (long long)(list), (long long)(size))
#define __sanitizer_syscall_pre_llistxattr(path, list, size)                   \
  __sanitizer_syscall_pre_impl_llistxattr(                                     \
      (long long)(path), (long long)(list), (long long)(size))
#define __sanitizer_syscall_post_llistxattr(res, path, list, size)             \
  __sanitizer_syscall_post_impl_llistxattr(                                    \
      res, (long long)(path), (long long)(list), (long long)(size))
#define __sanitizer_syscall_pre_flistxattr(fd, list, size)                     \
  __sanitizer_syscall_pre_impl_flistxattr((long long)(fd), (long long)(list),  \
                                          (long long)(size))
#define __sanitizer_syscall_post_flistxattr(res, fd, list, size)               \
  __sanitizer_syscall_post_impl_flistxattr(                                    \
      res, (long long)(fd), (long long)(list), (long long)(size))
#define __sanitizer_syscall_pre_removexattr(path, name)                        \
  __sanitizer_syscall_pre_impl_removexattr((long long)(path), (long long)(name))
#define __sanitizer_syscall_post_removexattr(res, path, name)                  \
  __sanitizer_syscall_post_impl_removexattr(res, (long long)(path),            \
                                            (long long)(name))
#define __sanitizer_syscall_pre_lremovexattr(path, name)                       \
  __sanitizer_syscall_pre_impl_lremovexattr((long long)(path),                 \
                                            (long long)(name))
#define __sanitizer_syscall_post_lremovexattr(res, path, name)                 \
  __sanitizer_syscall_post_impl_lremovexattr(res, (long long)(path),           \
                                             (long long)(name))
#define __sanitizer_syscall_pre_fremovexattr(fd, name)                         \
  __sanitizer_syscall_pre_impl_fremovexattr((long long)(fd), (long long)(name))
#define __sanitizer_syscall_post_fremovexattr(res, fd, name)                   \
  __sanitizer_syscall_post_impl_fremovexattr(res, (long long)(fd),             \
                                             (long long)(name))
#define __sanitizer_syscall_pre_compat_50___stat30(path, ub)                   \
  __sanitizer_syscall_pre_impl_compat_50___stat30((long long)(path),           \
                                                  (long long)(ub))
#define __sanitizer_syscall_post_compat_50___stat30(res, path, ub)             \
  __sanitizer_syscall_post_impl_compat_50___stat30(res, (long long)(path),     \
                                                   (long long)(ub))
#define __sanitizer_syscall_pre_compat_50___fstat30(fd, sb)                    \
  __sanitizer_syscall_pre_impl_compat_50___fstat30((long long)(fd),            \
                                                   (long long)(sb))
#define __sanitizer_syscall_post_compat_50___fstat30(res, fd, sb)              \
  __sanitizer_syscall_post_impl_compat_50___fstat30(res, (long long)(fd),      \
                                                    (long long)(sb))
#define __sanitizer_syscall_pre_compat_50___lstat30(path, ub)                  \
  __sanitizer_syscall_pre_impl_compat_50___lstat30((long long)(path),          \
                                                   (long long)(ub))
#define __sanitizer_syscall_post_compat_50___lstat30(res, path, ub)            \
  __sanitizer_syscall_post_impl_compat_50___lstat30(res, (long long)(path),    \
                                                    (long long)(ub))
#define __sanitizer_syscall_pre___getdents30(fd, buf, count)                   \
  __sanitizer_syscall_pre_impl___getdents30((long long)(fd), (long long)(buf), \
                                            (long long)(count))
#define __sanitizer_syscall_post___getdents30(res, fd, buf, count)             \
  __sanitizer_syscall_post_impl___getdents30(                                  \
      res, (long long)(fd), (long long)(buf), (long long)(count))
#define __sanitizer_syscall_pre_posix_fadvise()                                \
  __sanitizer_syscall_pre_impl_posix_fadvise((long long)())
#define __sanitizer_syscall_post_posix_fadvise(res)                            \
  __sanitizer_syscall_post_impl_posix_fadvise(res, (long long)())
#define __sanitizer_syscall_pre_compat_30___fhstat30(fhp, sb)                  \
  __sanitizer_syscall_pre_impl_compat_30___fhstat30((long long)(fhp),          \
                                                    (long long)(sb))
#define __sanitizer_syscall_post_compat_30___fhstat30(res, fhp, sb)            \
  __sanitizer_syscall_post_impl_compat_30___fhstat30(res, (long long)(fhp),    \
                                                     (long long)(sb))
#define __sanitizer_syscall_pre_compat_50___ntp_gettime30(ntvp)                \
  __sanitizer_syscall_pre_impl_compat_50___ntp_gettime30((long long)(ntvp))
#define __sanitizer_syscall_post_compat_50___ntp_gettime30(res, ntvp)          \
  __sanitizer_syscall_post_impl_compat_50___ntp_gettime30(res,                 \
                                                          (long long)(ntvp))
#define __sanitizer_syscall_pre___socket30(domain, type, protocol)             \
  __sanitizer_syscall_pre_impl___socket30(                                     \
      (long long)(domain), (long long)(type), (long long)(protocol))
#define __sanitizer_syscall_post___socket30(res, domain, type, protocol)       \
  __sanitizer_syscall_post_impl___socket30(                                    \
      res, (long long)(domain), (long long)(type), (long long)(protocol))
#define __sanitizer_syscall_pre___getfh30(fname, fhp, fh_size)                 \
  __sanitizer_syscall_pre_impl___getfh30((long long)(fname), (long long)(fhp), \
                                         (long long)(fh_size))
#define __sanitizer_syscall_post___getfh30(res, fname, fhp, fh_size)           \
  __sanitizer_syscall_post_impl___getfh30(                                     \
      res, (long long)(fname), (long long)(fhp), (long long)(fh_size))
#define __sanitizer_syscall_pre___fhopen40(fhp, fh_size, flags)                \
  __sanitizer_syscall_pre_impl___fhopen40(                                     \
      (long long)(fhp), (long long)(fh_size), (long long)(flags))
#define __sanitizer_syscall_post___fhopen40(res, fhp, fh_size, flags)          \
  __sanitizer_syscall_post_impl___fhopen40(                                    \
      res, (long long)(fhp), (long long)(fh_size), (long long)(flags))
#define __sanitizer_syscall_pre___fhstatvfs140(fhp, fh_size, buf, flags)       \
  __sanitizer_syscall_pre_impl___fhstatvfs140(                                 \
      (long long)(fhp), (long long)(fh_size), (long long)(buf),                \
      (long long)(flags))
#define __sanitizer_syscall_post___fhstatvfs140(res, fhp, fh_size, buf, flags) \
  __sanitizer_syscall_post_impl___fhstatvfs140(                                \
      res, (long long)(fhp), (long long)(fh_size), (long long)(buf),           \
      (long long)(flags))
#define __sanitizer_syscall_pre_compat_50___fhstat40(fhp, fh_size, sb)         \
  __sanitizer_syscall_pre_impl_compat_50___fhstat40(                           \
      (long long)(fhp), (long long)(fh_size), (long long)(sb))
#define __sanitizer_syscall_post_compat_50___fhstat40(res, fhp, fh_size, sb)   \
  __sanitizer_syscall_post_impl_compat_50___fhstat40(                          \
      res, (long long)(fhp), (long long)(fh_size), (long long)(sb))
#define __sanitizer_syscall_pre_aio_cancel(fildes, aiocbp)                     \
  __sanitizer_syscall_pre_impl_aio_cancel((long long)(fildes),                 \
                                          (long long)(aiocbp))
#define __sanitizer_syscall_post_aio_cancel(res, fildes, aiocbp)               \
  __sanitizer_syscall_post_impl_aio_cancel(res, (long long)(fildes),           \
                                           (long long)(aiocbp))
#define __sanitizer_syscall_pre_aio_error(aiocbp)                              \
  __sanitizer_syscall_pre_impl_aio_error((long long)(aiocbp))
#define __sanitizer_syscall_post_aio_error(res, aiocbp)                        \
  __sanitizer_syscall_post_impl_aio_error(res, (long long)(aiocbp))
#define __sanitizer_syscall_pre_aio_fsync(op, aiocbp)                          \
  __sanitizer_syscall_pre_impl_aio_fsync((long long)(op), (long long)(aiocbp))
#define __sanitizer_syscall_post_aio_fsync(res, op, aiocbp)                    \
  __sanitizer_syscall_post_impl_aio_fsync(res, (long long)(op),                \
                                          (long long)(aiocbp))
#define __sanitizer_syscall_pre_aio_read(aiocbp)                               \
  __sanitizer_syscall_pre_impl_aio_read((long long)(aiocbp))
#define __sanitizer_syscall_post_aio_read(res, aiocbp)                         \
  __sanitizer_syscall_post_impl_aio_read(res, (long long)(aiocbp))
#define __sanitizer_syscall_pre_aio_return(aiocbp)                             \
  __sanitizer_syscall_pre_impl_aio_return((long long)(aiocbp))
#define __sanitizer_syscall_post_aio_return(res, aiocbp)                       \
  __sanitizer_syscall_post_impl_aio_return(res, (long long)(aiocbp))
#define __sanitizer_syscall_pre_compat_50_aio_suspend(list, nent, timeout)     \
  __sanitizer_syscall_pre_impl_compat_50_aio_suspend(                          \
      (long long)(list), (long long)(nent), (long long)(timeout))
#define __sanitizer_syscall_post_compat_50_aio_suspend(res, list, nent,        \
                                                       timeout)                \
  __sanitizer_syscall_post_impl_compat_50_aio_suspend(                         \
      res, (long long)(list), (long long)(nent), (long long)(timeout))
#define __sanitizer_syscall_pre_aio_write(aiocbp)                              \
  __sanitizer_syscall_pre_impl_aio_write((long long)(aiocbp))
#define __sanitizer_syscall_post_aio_write(res, aiocbp)                        \
  __sanitizer_syscall_post_impl_aio_write(res, (long long)(aiocbp))
#define __sanitizer_syscall_pre_lio_listio(mode, list, nent, sig)              \
  __sanitizer_syscall_pre_impl_lio_listio((long long)(mode),                   \
                                          (long long)(list),                   \
                                          (long long)(nent), (long long)(sig))
#define __sanitizer_syscall_post_lio_listio(res, mode, list, nent, sig)        \
  __sanitizer_syscall_post_impl_lio_listio(                                    \
      res, (long long)(mode), (long long)(list), (long long)(nent),            \
      (long long)(sig))
/* syscall 407 has been skipped */
/* syscall 408 has been skipped */
/* syscall 409 has been skipped */
#define __sanitizer_syscall_pre___mount50(type, path, flags, data, data_len)   \
  __sanitizer_syscall_pre_impl___mount50(                                      \
      (long long)(type), (long long)(path), (long long)(flags),                \
      (long long)(data), (long long)(data_len))
#define __sanitizer_syscall_post___mount50(res, type, path, flags, data,       \
                                           data_len)                           \
  __sanitizer_syscall_post_impl___mount50(                                     \
      res, (long long)(type), (long long)(path), (long long)(flags),           \
      (long long)(data), (long long)(data_len))
#define __sanitizer_syscall_pre_mremap(old_address, old_size, new_address,     \
                                       new_size, flags)                        \
  __sanitizer_syscall_pre_impl_mremap(                                         \
      (long long)(old_address), (long long)(old_size),                         \
      (long long)(new_address), (long long)(new_size), (long long)(flags))
#define __sanitizer_syscall_post_mremap(res, old_address, old_size,            \
                                        new_address, new_size, flags)          \
  __sanitizer_syscall_post_impl_mremap(                                        \
      res, (long long)(old_address), (long long)(old_size),                    \
      (long long)(new_address), (long long)(new_size), (long long)(flags))
#define __sanitizer_syscall_pre_pset_create(psid)                              \
  __sanitizer_syscall_pre_impl_pset_create((long long)(psid))
#define __sanitizer_syscall_post_pset_create(res, psid)                        \
  __sanitizer_syscall_post_impl_pset_create(res, (long long)(psid))
#define __sanitizer_syscall_pre_pset_destroy(psid)                             \
  __sanitizer_syscall_pre_impl_pset_destroy((long long)(psid))
#define __sanitizer_syscall_post_pset_destroy(res, psid)                       \
  __sanitizer_syscall_post_impl_pset_destroy(res, (long long)(psid))
#define __sanitizer_syscall_pre_pset_assign(psid, cpuid, opsid)                \
  __sanitizer_syscall_pre_impl_pset_assign(                                    \
      (long long)(psid), (long long)(cpuid), (long long)(opsid))
#define __sanitizer_syscall_post_pset_assign(res, psid, cpuid, opsid)          \
  __sanitizer_syscall_post_impl_pset_assign(                                   \
      res, (long long)(psid), (long long)(cpuid), (long long)(opsid))
#define __sanitizer_syscall_pre__pset_bind(idtype, first_id, second_id, psid,  \
                                           opsid)                              \
  __sanitizer_syscall_pre_impl__pset_bind(                                     \
      (long long)(idtype), (long long)(first_id), (long long)(second_id),      \
      (long long)(psid), (long long)(opsid))
#define __sanitizer_syscall_post__pset_bind(res, idtype, first_id, second_id,  \
                                            psid, opsid)                       \
  __sanitizer_syscall_post_impl__pset_bind(                                    \
      res, (long long)(idtype), (long long)(first_id), (long long)(second_id), \
      (long long)(psid), (long long)(opsid))
#define __sanitizer_syscall_pre___posix_fadvise50(fd, PAD, offset, len,        \
                                                  advice)                      \
  __sanitizer_syscall_pre_impl___posix_fadvise50(                              \
      (long long)(fd), (long long)(PAD), (long long)(offset),                  \
      (long long)(len), (long long)(advice))
#define __sanitizer_syscall_post___posix_fadvise50(res, fd, PAD, offset, len,  \
                                                   advice)                     \
  __sanitizer_syscall_post_impl___posix_fadvise50(                             \
      res, (long long)(fd), (long long)(PAD), (long long)(offset),             \
      (long long)(len), (long long)(advice))
#define __sanitizer_syscall_pre___select50(nd, in, ou, ex, tv)                 \
  __sanitizer_syscall_pre_impl___select50((long long)(nd), (long long)(in),    \
                                          (long long)(ou), (long long)(ex),    \
                                          (long long)(tv))
#define __sanitizer_syscall_post___select50(res, nd, in, ou, ex, tv)           \
  __sanitizer_syscall_post_impl___select50(res, (long long)(nd),               \
                                           (long long)(in), (long long)(ou),   \
                                           (long long)(ex), (long long)(tv))
#define __sanitizer_syscall_pre___gettimeofday50(tp, tzp)                      \
  __sanitizer_syscall_pre_impl___gettimeofday50((long long)(tp),               \
                                                (long long)(tzp))
#define __sanitizer_syscall_post___gettimeofday50(res, tp, tzp)                \
  __sanitizer_syscall_post_impl___gettimeofday50(res, (long long)(tp),         \
                                                 (long long)(tzp))
#define __sanitizer_syscall_pre___settimeofday50(tv, tzp)                      \
  __sanitizer_syscall_pre_impl___settimeofday50((long long)(tv),               \
                                                (long long)(tzp))
#define __sanitizer_syscall_post___settimeofday50(res, tv, tzp)                \
  __sanitizer_syscall_post_impl___settimeofday50(res, (long long)(tv),         \
                                                 (long long)(tzp))
#define __sanitizer_syscall_pre___utimes50(path, tptr)                         \
  __sanitizer_syscall_pre_impl___utimes50((long long)(path), (long long)(tptr))
#define __sanitizer_syscall_post___utimes50(res, path, tptr)                   \
  __sanitizer_syscall_post_impl___utimes50(res, (long long)(path),             \
                                           (long long)(tptr))
#define __sanitizer_syscall_pre___adjtime50(delta, olddelta)                   \
  __sanitizer_syscall_pre_impl___adjtime50((long long)(delta),                 \
                                           (long long)(olddelta))
#define __sanitizer_syscall_post___adjtime50(res, delta, olddelta)             \
  __sanitizer_syscall_post_impl___adjtime50(res, (long long)(delta),           \
                                            (long long)(olddelta))
#define __sanitizer_syscall_pre___lfs_segwait50(fsidp, tv)                     \
  __sanitizer_syscall_pre_impl___lfs_segwait50((long long)(fsidp),             \
                                               (long long)(tv))
#define __sanitizer_syscall_post___lfs_segwait50(res, fsidp, tv)               \
  __sanitizer_syscall_post_impl___lfs_segwait50(res, (long long)(fsidp),       \
                                                (long long)(tv))
#define __sanitizer_syscall_pre___futimes50(fd, tptr)                          \
  __sanitizer_syscall_pre_impl___futimes50((long long)(fd), (long long)(tptr))
#define __sanitizer_syscall_post___futimes50(res, fd, tptr)                    \
  __sanitizer_syscall_post_impl___futimes50(res, (long long)(fd),              \
                                            (long long)(tptr))
#define __sanitizer_syscall_pre___lutimes50(path, tptr)                        \
  __sanitizer_syscall_pre_impl___lutimes50((long long)(path), (long long)(tptr))
#define __sanitizer_syscall_post___lutimes50(res, path, tptr)                  \
  __sanitizer_syscall_post_impl___lutimes50(res, (long long)(path),            \
                                            (long long)(tptr))
#define __sanitizer_syscall_pre___setitimer50(which, itv, oitv)                \
  __sanitizer_syscall_pre_impl___setitimer50(                                  \
      (long long)(which), (long long)(itv), (long long)(oitv))
#define __sanitizer_syscall_post___setitimer50(res, which, itv, oitv)          \
  __sanitizer_syscall_post_impl___setitimer50(                                 \
      res, (long long)(which), (long long)(itv), (long long)(oitv))
#define __sanitizer_syscall_pre___getitimer50(which, itv)                      \
  __sanitizer_syscall_pre_impl___getitimer50((long long)(which),               \
                                             (long long)(itv))
#define __sanitizer_syscall_post___getitimer50(res, which, itv)                \
  __sanitizer_syscall_post_impl___getitimer50(res, (long long)(which),         \
                                              (long long)(itv))
#define __sanitizer_syscall_pre___clock_gettime50(clock_id, tp)                \
  __sanitizer_syscall_pre_impl___clock_gettime50((long long)(clock_id),        \
                                                 (long long)(tp))
#define __sanitizer_syscall_post___clock_gettime50(res, clock_id, tp)          \
  __sanitizer_syscall_post_impl___clock_gettime50(res, (long long)(clock_id),  \
                                                  (long long)(tp))
#define __sanitizer_syscall_pre___clock_settime50(clock_id, tp)                \
  __sanitizer_syscall_pre_impl___clock_settime50((long long)(clock_id),        \
                                                 (long long)(tp))
#define __sanitizer_syscall_post___clock_settime50(res, clock_id, tp)          \
  __sanitizer_syscall_post_impl___clock_settime50(res, (long long)(clock_id),  \
                                                  (long long)(tp))
#define __sanitizer_syscall_pre___clock_getres50(clock_id, tp)                 \
  __sanitizer_syscall_pre_impl___clock_getres50((long long)(clock_id),         \
                                                (long long)(tp))
#define __sanitizer_syscall_post___clock_getres50(res, clock_id, tp)           \
  __sanitizer_syscall_post_impl___clock_getres50(res, (long long)(clock_id),   \
                                                 (long long)(tp))
#define __sanitizer_syscall_pre___nanosleep50(rqtp, rmtp)                      \
  __sanitizer_syscall_pre_impl___nanosleep50((long long)(rqtp),                \
                                             (long long)(rmtp))
#define __sanitizer_syscall_post___nanosleep50(res, rqtp, rmtp)                \
  __sanitizer_syscall_post_impl___nanosleep50(res, (long long)(rqtp),          \
                                              (long long)(rmtp))
#define __sanitizer_syscall_pre_____sigtimedwait50(set, info, timeout)         \
  __sanitizer_syscall_pre_impl_____sigtimedwait50(                             \
      (long long)(set), (long long)(info), (long long)(timeout))
#define __sanitizer_syscall_post_____sigtimedwait50(res, set, info, timeout)   \
  __sanitizer_syscall_post_impl_____sigtimedwait50(                            \
      res, (long long)(set), (long long)(info), (long long)(timeout))
#define __sanitizer_syscall_pre___mq_timedsend50(mqdes, msg_ptr, msg_len,      \
                                                 msg_prio, abs_timeout)        \
  __sanitizer_syscall_pre_impl___mq_timedsend50(                               \
      (long long)(mqdes), (long long)(msg_ptr), (long long)(msg_len),          \
      (long long)(msg_prio), (long long)(abs_timeout))
#define __sanitizer_syscall_post___mq_timedsend50(                             \
    res, mqdes, msg_ptr, msg_len, msg_prio, abs_timeout)                       \
  __sanitizer_syscall_post_impl___mq_timedsend50(                              \
      res, (long long)(mqdes), (long long)(msg_ptr), (long long)(msg_len),     \
      (long long)(msg_prio), (long long)(abs_timeout))
#define __sanitizer_syscall_pre___mq_timedreceive50(mqdes, msg_ptr, msg_len,   \
                                                    msg_prio, abs_timeout)     \
  __sanitizer_syscall_pre_impl___mq_timedreceive50(                            \
      (long long)(mqdes), (long long)(msg_ptr), (long long)(msg_len),          \
      (long long)(msg_prio), (long long)(abs_timeout))
#define __sanitizer_syscall_post___mq_timedreceive50(                          \
    res, mqdes, msg_ptr, msg_len, msg_prio, abs_timeout)                       \
  __sanitizer_syscall_post_impl___mq_timedreceive50(                           \
      res, (long long)(mqdes), (long long)(msg_ptr), (long long)(msg_len),     \
      (long long)(msg_prio), (long long)(abs_timeout))
#define __sanitizer_syscall_pre_compat_60__lwp_park(ts, unpark, hint,          \
                                                    unparkhint)                \
  __sanitizer_syscall_pre_impl_compat_60__lwp_park(                            \
      (long long)(ts), (long long)(unpark), (long long)(hint),                 \
      (long long)(unparkhint))
#define __sanitizer_syscall_post_compat_60__lwp_park(res, ts, unpark, hint,    \
                                                     unparkhint)               \
  __sanitizer_syscall_post_impl_compat_60__lwp_park(                           \
      res, (long long)(ts), (long long)(unpark), (long long)(hint),            \
      (long long)(unparkhint))
#define __sanitizer_syscall_pre___kevent50(fd, changelist, nchanges,           \
                                           eventlist, nevents, timeout)        \
  __sanitizer_syscall_pre_impl___kevent50(                                     \
      (long long)(fd), (long long)(changelist), (long long)(nchanges),         \
      (long long)(eventlist), (long long)(nevents), (long long)(timeout))
#define __sanitizer_syscall_post___kevent50(res, fd, changelist, nchanges,     \
                                            eventlist, nevents, timeout)       \
  __sanitizer_syscall_post_impl___kevent50(                                    \
      res, (long long)(fd), (long long)(changelist), (long long)(nchanges),    \
      (long long)(eventlist), (long long)(nevents), (long long)(timeout))
#define __sanitizer_syscall_pre___pselect50(nd, in, ou, ex, ts, mask)          \
  __sanitizer_syscall_pre_impl___pselect50((long long)(nd), (long long)(in),   \
                                           (long long)(ou), (long long)(ex),   \
                                           (long long)(ts), (long long)(mask))
#define __sanitizer_syscall_post___pselect50(res, nd, in, ou, ex, ts, mask)    \
  __sanitizer_syscall_post_impl___pselect50(                                   \
      res, (long long)(nd), (long long)(in), (long long)(ou), (long long)(ex), \
      (long long)(ts), (long long)(mask))
#define __sanitizer_syscall_pre___pollts50(fds, nfds, ts, mask)                \
  __sanitizer_syscall_pre_impl___pollts50((long long)(fds), (long long)(nfds), \
                                          (long long)(ts), (long long)(mask))
#define __sanitizer_syscall_post___pollts50(res, fds, nfds, ts, mask)          \
  __sanitizer_syscall_post_impl___pollts50(res, (long long)(fds),              \
                                           (long long)(nfds), (long long)(ts), \
                                           (long long)(mask))
#define __sanitizer_syscall_pre___aio_suspend50(list, nent, timeout)           \
  __sanitizer_syscall_pre_impl___aio_suspend50(                                \
      (long long)(list), (long long)(nent), (long long)(timeout))
#define __sanitizer_syscall_post___aio_suspend50(res, list, nent, timeout)     \
  __sanitizer_syscall_post_impl___aio_suspend50(                               \
      res, (long long)(list), (long long)(nent), (long long)(timeout))
#define __sanitizer_syscall_pre___stat50(path, ub)                             \
  __sanitizer_syscall_pre_impl___stat50((long long)(path), (long long)(ub))
#define __sanitizer_syscall_post___stat50(res, path, ub)                       \
  __sanitizer_syscall_post_impl___stat50(res, (long long)(path),               \
                                         (long long)(ub))
#define __sanitizer_syscall_pre___fstat50(fd, sb)                              \
  __sanitizer_syscall_pre_impl___fstat50((long long)(fd), (long long)(sb))
#define __sanitizer_syscall_post___fstat50(res, fd, sb)                        \
  __sanitizer_syscall_post_impl___fstat50(res, (long long)(fd), (long long)(sb))
#define __sanitizer_syscall_pre___lstat50(path, ub)                            \
  __sanitizer_syscall_pre_impl___lstat50((long long)(path), (long long)(ub))
#define __sanitizer_syscall_post___lstat50(res, path, ub)                      \
  __sanitizer_syscall_post_impl___lstat50(res, (long long)(path),              \
                                          (long long)(ub))
#define __sanitizer_syscall_pre_____semctl50(semid, semnum, cmd, arg)          \
  __sanitizer_syscall_pre_impl_____semctl50(                                   \
      (long long)(semid), (long long)(semnum), (long long)(cmd),               \
      (long long)(arg))
#define __sanitizer_syscall_post_____semctl50(res, semid, semnum, cmd, arg)    \
  __sanitizer_syscall_post_impl_____semctl50(                                  \
      res, (long long)(semid), (long long)(semnum), (long long)(cmd),          \
      (long long)(arg))
#define __sanitizer_syscall_pre___shmctl50(shmid, cmd, buf)                    \
  __sanitizer_syscall_pre_impl___shmctl50((long long)(shmid),                  \
                                          (long long)(cmd), (long long)(buf))
#define __sanitizer_syscall_post___shmctl50(res, shmid, cmd, buf)              \
  __sanitizer_syscall_post_impl___shmctl50(res, (long long)(shmid),            \
                                           (long long)(cmd), (long long)(buf))
#define __sanitizer_syscall_pre___msgctl50(msqid, cmd, buf)                    \
  __sanitizer_syscall_pre_impl___msgctl50((long long)(msqid),                  \
                                          (long long)(cmd), (long long)(buf))
#define __sanitizer_syscall_post___msgctl50(res, msqid, cmd, buf)              \
  __sanitizer_syscall_post_impl___msgctl50(res, (long long)(msqid),            \
                                           (long long)(cmd), (long long)(buf))
#define __sanitizer_syscall_pre___getrusage50(who, rusage)                     \
  __sanitizer_syscall_pre_impl___getrusage50((long long)(who),                 \
                                             (long long)(rusage))
#define __sanitizer_syscall_post___getrusage50(res, who, rusage)               \
  __sanitizer_syscall_post_impl___getrusage50(res, (long long)(who),           \
                                              (long long)(rusage))
#define __sanitizer_syscall_pre___timer_settime50(timerid, flags, value,       \
                                                  ovalue)                      \
  __sanitizer_syscall_pre_impl___timer_settime50(                              \
      (long long)(timerid), (long long)(flags), (long long)(value),            \
      (long long)(ovalue))
#define __sanitizer_syscall_post___timer_settime50(res, timerid, flags, value, \
                                                   ovalue)                     \
  __sanitizer_syscall_post_impl___timer_settime50(                             \
      res, (long long)(timerid), (long long)(flags), (long long)(value),       \
      (long long)(ovalue))
#define __sanitizer_syscall_pre___timer_gettime50(timerid, value)              \
  __sanitizer_syscall_pre_impl___timer_gettime50((long long)(timerid),         \
                                                 (long long)(value))
#define __sanitizer_syscall_post___timer_gettime50(res, timerid, value)        \
  __sanitizer_syscall_post_impl___timer_gettime50(res, (long long)(timerid),   \
                                                  (long long)(value))
#if defined(NTP) || !defined(_KERNEL_OPT)
#define __sanitizer_syscall_pre___ntp_gettime50(ntvp)                          \
  __sanitizer_syscall_pre_impl___ntp_gettime50((long long)(ntvp))
#define __sanitizer_syscall_post___ntp_gettime50(res, ntvp)                    \
  __sanitizer_syscall_post_impl___ntp_gettime50(res, (long long)(ntvp))
#else
/* syscall 448 has been skipped */
#endif
#define __sanitizer_syscall_pre___wait450(pid, status, options, rusage)        \
  __sanitizer_syscall_pre_impl___wait450(                                      \
      (long long)(pid), (long long)(status), (long long)(options),             \
      (long long)(rusage))
#define __sanitizer_syscall_post___wait450(res, pid, status, options, rusage)  \
  __sanitizer_syscall_post_impl___wait450(                                     \
      res, (long long)(pid), (long long)(status), (long long)(options),        \
      (long long)(rusage))
#define __sanitizer_syscall_pre___mknod50(path, mode, dev)                     \
  __sanitizer_syscall_pre_impl___mknod50((long long)(path), (long long)(mode), \
                                         (long long)(dev))
#define __sanitizer_syscall_post___mknod50(res, path, mode, dev)               \
  __sanitizer_syscall_post_impl___mknod50(res, (long long)(path),              \
                                          (long long)(mode), (long long)(dev))
#define __sanitizer_syscall_pre___fhstat50(fhp, fh_size, sb)                   \
  __sanitizer_syscall_pre_impl___fhstat50(                                     \
      (long long)(fhp), (long long)(fh_size), (long long)(sb))
#define __sanitizer_syscall_post___fhstat50(res, fhp, fh_size, sb)             \
  __sanitizer_syscall_post_impl___fhstat50(                                    \
      res, (long long)(fhp), (long long)(fh_size), (long long)(sb))
/* syscall 452 has been skipped */
#define __sanitizer_syscall_pre_pipe2(fildes, flags)                           \
  __sanitizer_syscall_pre_impl_pipe2((long long)(fildes), (long long)(flags))
#define __sanitizer_syscall_post_pipe2(res, fildes, flags)                     \
  __sanitizer_syscall_post_impl_pipe2(res, (long long)(fildes),                \
                                      (long long)(flags))
#define __sanitizer_syscall_pre_dup3(from, to, flags)                          \
  __sanitizer_syscall_pre_impl_dup3((long long)(from), (long long)(to),        \
                                    (long long)(flags))
#define __sanitizer_syscall_post_dup3(res, from, to, flags)                    \
  __sanitizer_syscall_post_impl_dup3(res, (long long)(from), (long long)(to),  \
                                     (long long)(flags))
#define __sanitizer_syscall_pre_kqueue1(flags)                                 \
  __sanitizer_syscall_pre_impl_kqueue1((long long)(flags))
#define __sanitizer_syscall_post_kqueue1(res, flags)                           \
  __sanitizer_syscall_post_impl_kqueue1(res, (long long)(flags))
#define __sanitizer_syscall_pre_paccept(s, name, anamelen, mask, flags)        \
  __sanitizer_syscall_pre_impl_paccept((long long)(s), (long long)(name),      \
                                       (long long)(anamelen),                  \
                                       (long long)(mask), (long long)(flags))
#define __sanitizer_syscall_post_paccept(res, s, name, anamelen, mask, flags)  \
  __sanitizer_syscall_post_impl_paccept(                                       \
      res, (long long)(s), (long long)(name), (long long)(anamelen),           \
      (long long)(mask), (long long)(flags))
#define __sanitizer_syscall_pre_linkat(fd1, name1, fd2, name2, flags)          \
  __sanitizer_syscall_pre_impl_linkat((long long)(fd1), (long long)(name1),    \
                                      (long long)(fd2), (long long)(name2),    \
                                      (long long)(flags))
#define __sanitizer_syscall_post_linkat(res, fd1, name1, fd2, name2, flags)    \
  __sanitizer_syscall_post_impl_linkat(res, (long long)(fd1),                  \
                                       (long long)(name1), (long long)(fd2),   \
                                       (long long)(name2), (long long)(flags))
#define __sanitizer_syscall_pre_renameat(fromfd, from, tofd, to)               \
  __sanitizer_syscall_pre_impl_renameat((long long)(fromfd),                   \
                                        (long long)(from), (long long)(tofd),  \
                                        (long long)(to))
#define __sanitizer_syscall_post_renameat(res, fromfd, from, tofd, to)         \
  __sanitizer_syscall_post_impl_renameat(res, (long long)(fromfd),             \
                                         (long long)(from), (long long)(tofd), \
                                         (long long)(to))
#define __sanitizer_syscall_pre_mkfifoat(fd, path, mode)                       \
  __sanitizer_syscall_pre_impl_mkfifoat((long long)(fd), (long long)(path),    \
                                        (long long)(mode))
#define __sanitizer_syscall_post_mkfifoat(res, fd, path, mode)                 \
  __sanitizer_syscall_post_impl_mkfifoat(res, (long long)(fd),                 \
                                         (long long)(path), (long long)(mode))
#define __sanitizer_syscall_pre_mknodat(fd, path, mode, PAD, dev)              \
  __sanitizer_syscall_pre_impl_mknodat((long long)(fd), (long long)(path),     \
                                       (long long)(mode), (long long)(PAD),    \
                                       (long long)(dev))
#define __sanitizer_syscall_post_mknodat(res, fd, path, mode, PAD, dev)        \
  __sanitizer_syscall_post_impl_mknodat(res, (long long)(fd),                  \
                                        (long long)(path), (long long)(mode),  \
                                        (long long)(PAD), (long long)(dev))
#define __sanitizer_syscall_pre_mkdirat(fd, path, mode)                        \
  __sanitizer_syscall_pre_impl_mkdirat((long long)(fd), (long long)(path),     \
                                       (long long)(mode))
#define __sanitizer_syscall_post_mkdirat(res, fd, path, mode)                  \
  __sanitizer_syscall_post_impl_mkdirat(res, (long long)(fd),                  \
                                        (long long)(path), (long long)(mode))
#define __sanitizer_syscall_pre_faccessat(fd, path, amode, flag)               \
  __sanitizer_syscall_pre_impl_faccessat((long long)(fd), (long long)(path),   \
                                         (long long)(amode),                   \
                                         (long long)(flag))
#define __sanitizer_syscall_post_faccessat(res, fd, path, amode, flag)         \
  __sanitizer_syscall_post_impl_faccessat(                                     \
      res, (long long)(fd), (long long)(path), (long long)(amode),             \
      (long long)(flag))
#define __sanitizer_syscall_pre_fchmodat(fd, path, mode, flag)                 \
  __sanitizer_syscall_pre_impl_fchmodat((long long)(fd), (long long)(path),    \
                                        (long long)(mode), (long long)(flag))
#define __sanitizer_syscall_post_fchmodat(res, fd, path, mode, flag)           \
  __sanitizer_syscall_post_impl_fchmodat(res, (long long)(fd),                 \
                                         (long long)(path), (long long)(mode), \
                                         (long long)(flag))
#define __sanitizer_syscall_pre_fchownat(fd, path, owner, group, flag)         \
  __sanitizer_syscall_pre_impl_fchownat((long long)(fd), (long long)(path),    \
                                        (long long)(owner),                    \
                                        (long long)(group), (long long)(flag))
#define __sanitizer_syscall_post_fchownat(res, fd, path, owner, group, flag)   \
  __sanitizer_syscall_post_impl_fchownat(                                      \
      res, (long long)(fd), (long long)(path), (long long)(owner),             \
      (long long)(group), (long long)(flag))
#define __sanitizer_syscall_pre_fexecve(fd, argp, envp)                        \
  __sanitizer_syscall_pre_impl_fexecve((long long)(fd), (long long)(argp),     \
                                       (long long)(envp))
#define __sanitizer_syscall_post_fexecve(res, fd, argp, envp)                  \
  __sanitizer_syscall_post_impl_fexecve(res, (long long)(fd),                  \
                                        (long long)(argp), (long long)(envp))
#define __sanitizer_syscall_pre_fstatat(fd, path, buf, flag)                   \
  __sanitizer_syscall_pre_impl_fstatat((long long)(fd), (long long)(path),     \
                                       (long long)(buf), (long long)(flag))
#define __sanitizer_syscall_post_fstatat(res, fd, path, buf, flag)             \
  __sanitizer_syscall_post_impl_fstatat(res, (long long)(fd),                  \
                                        (long long)(path), (long long)(buf),   \
                                        (long long)(flag))
#define __sanitizer_syscall_pre_utimensat(fd, path, tptr, flag)                \
  __sanitizer_syscall_pre_impl_utimensat((long long)(fd), (long long)(path),   \
                                         (long long)(tptr), (long long)(flag))
#define __sanitizer_syscall_post_utimensat(res, fd, path, tptr, flag)          \
  __sanitizer_syscall_post_impl_utimensat(                                     \
      res, (long long)(fd), (long long)(path), (long long)(tptr),              \
      (long long)(flag))
#define __sanitizer_syscall_pre_openat(fd, path, oflags, mode)                 \
  __sanitizer_syscall_pre_impl_openat((long long)(fd), (long long)(path),      \
                                      (long long)(oflags), (long long)(mode))
#define __sanitizer_syscall_post_openat(res, fd, path, oflags, mode)           \
  __sanitizer_syscall_post_impl_openat(res, (long long)(fd),                   \
                                       (long long)(path), (long long)(oflags), \
                                       (long long)(mode))
#define __sanitizer_syscall_pre_readlinkat(fd, path, buf, bufsize)             \
  __sanitizer_syscall_pre_impl_readlinkat((long long)(fd), (long long)(path),  \
                                          (long long)(buf),                    \
                                          (long long)(bufsize))
#define __sanitizer_syscall_post_readlinkat(res, fd, path, buf, bufsize)       \
  __sanitizer_syscall_post_impl_readlinkat(                                    \
      res, (long long)(fd), (long long)(path), (long long)(buf),               \
      (long long)(bufsize))
#define __sanitizer_syscall_pre_symlinkat(path1, fd, path2)                    \
  __sanitizer_syscall_pre_impl_symlinkat((long long)(path1), (long long)(fd),  \
                                         (long long)(path2))
#define __sanitizer_syscall_post_symlinkat(res, path1, fd, path2)              \
  __sanitizer_syscall_post_impl_symlinkat(res, (long long)(path1),             \
                                          (long long)(fd), (long long)(path2))
#define __sanitizer_syscall_pre_unlinkat(fd, path, flag)                       \
  __sanitizer_syscall_pre_impl_unlinkat((long long)(fd), (long long)(path),    \
                                        (long long)(flag))
#define __sanitizer_syscall_post_unlinkat(res, fd, path, flag)                 \
  __sanitizer_syscall_post_impl_unlinkat(res, (long long)(fd),                 \
                                         (long long)(path), (long long)(flag))
#define __sanitizer_syscall_pre_futimens(fd, tptr)                             \
  __sanitizer_syscall_pre_impl_futimens((long long)(fd), (long long)(tptr))
#define __sanitizer_syscall_post_futimens(res, fd, tptr)                       \
  __sanitizer_syscall_post_impl_futimens(res, (long long)(fd),                 \
                                         (long long)(tptr))
#define __sanitizer_syscall_pre___quotactl(path, args)                         \
  __sanitizer_syscall_pre_impl___quotactl((long long)(path), (long long)(args))
#define __sanitizer_syscall_post___quotactl(res, path, args)                   \
  __sanitizer_syscall_post_impl___quotactl(res, (long long)(path),             \
                                           (long long)(args))
#define __sanitizer_syscall_pre_posix_spawn(pid, path, file_actions, attrp,    \
                                            argv, envp)                        \
  __sanitizer_syscall_pre_impl_posix_spawn(                                    \
      (long long)(pid), (long long)(path), (long long)(file_actions),          \
      (long long)(attrp), (long long)(argv), (long long)(envp))
#define __sanitizer_syscall_post_posix_spawn(res, pid, path, file_actions,     \
                                             attrp, argv, envp)                \
  __sanitizer_syscall_post_impl_posix_spawn(                                   \
      res, (long long)(pid), (long long)(path), (long long)(file_actions),     \
      (long long)(attrp), (long long)(argv), (long long)(envp))
#define __sanitizer_syscall_pre_recvmmsg(s, mmsg, vlen, flags, timeout)        \
  __sanitizer_syscall_pre_impl_recvmmsg((long long)(s), (long long)(mmsg),     \
                                        (long long)(vlen), (long long)(flags), \
                                        (long long)(timeout))
#define __sanitizer_syscall_post_recvmmsg(res, s, mmsg, vlen, flags, timeout)  \
  __sanitizer_syscall_post_impl_recvmmsg(                                      \
      res, (long long)(s), (long long)(mmsg), (long long)(vlen),               \
      (long long)(flags), (long long)(timeout))
#define __sanitizer_syscall_pre_sendmmsg(s, mmsg, vlen, flags)                 \
  __sanitizer_syscall_pre_impl_sendmmsg((long long)(s), (long long)(mmsg),     \
                                        (long long)(vlen), (long long)(flags))
#define __sanitizer_syscall_post_sendmmsg(res, s, mmsg, vlen, flags)           \
  __sanitizer_syscall_post_impl_sendmmsg(res, (long long)(s),                  \
                                         (long long)(mmsg), (long long)(vlen), \
                                         (long long)(flags))
#define __sanitizer_syscall_pre_clock_nanosleep(clock_id, flags, rqtp, rmtp)   \
  __sanitizer_syscall_pre_impl_clock_nanosleep(                                \
      (long long)(clock_id), (long long)(flags), (long long)(rqtp),            \
      (long long)(rmtp))
#define __sanitizer_syscall_post_clock_nanosleep(res, clock_id, flags, rqtp,   \
                                                 rmtp)                         \
  __sanitizer_syscall_post_impl_clock_nanosleep(                               \
      res, (long long)(clock_id), (long long)(flags), (long long)(rqtp),       \
      (long long)(rmtp))
#define __sanitizer_syscall_pre____lwp_park60(clock_id, flags, ts, unpark,     \
                                              hint, unparkhint)                \
  __sanitizer_syscall_pre_impl____lwp_park60(                                  \
      (long long)(clock_id), (long long)(flags), (long long)(ts),              \
      (long long)(unpark), (long long)(hint), (long long)(unparkhint))
#define __sanitizer_syscall_post____lwp_park60(res, clock_id, flags, ts,       \
                                               unpark, hint, unparkhint)       \
  __sanitizer_syscall_post_impl____lwp_park60(                                 \
      res, (long long)(clock_id), (long long)(flags), (long long)(ts),         \
      (long long)(unpark), (long long)(hint), (long long)(unparkhint))
#define __sanitizer_syscall_pre_posix_fallocate(fd, PAD, pos, len)             \
  __sanitizer_syscall_pre_impl_posix_fallocate(                                \
      (long long)(fd), (long long)(PAD), (long long)(pos), (long long)(len))
#define __sanitizer_syscall_post_posix_fallocate(res, fd, PAD, pos, len)       \
  __sanitizer_syscall_post_impl_posix_fallocate(                               \
      res, (long long)(fd), (long long)(PAD), (long long)(pos),                \
      (long long)(len))
#define __sanitizer_syscall_pre_fdiscard(fd, PAD, pos, len)                    \
  __sanitizer_syscall_pre_impl_fdiscard((long long)(fd), (long long)(PAD),     \
                                        (long long)(pos), (long long)(len))
#define __sanitizer_syscall_post_fdiscard(res, fd, PAD, pos, len)              \
  __sanitizer_syscall_post_impl_fdiscard(res, (long long)(fd),                 \
                                         (long long)(PAD), (long long)(pos),   \
                                         (long long)(len))
#define __sanitizer_syscall_pre_wait6(idtype, id, status, options, wru, info)  \
  __sanitizer_syscall_pre_impl_wait6(                                          \
      (long long)(idtype), (long long)(id), (long long)(status),               \
      (long long)(options), (long long)(wru), (long long)(info))
#define __sanitizer_syscall_post_wait6(res, idtype, id, status, options, wru,  \
                                       info)                                   \
  __sanitizer_syscall_post_impl_wait6(                                         \
      res, (long long)(idtype), (long long)(id), (long long)(status),          \
      (long long)(options), (long long)(wru), (long long)(info))
#define __sanitizer_syscall_pre_clock_getcpuclockid2(idtype, id, clock_id)     \
  __sanitizer_syscall_pre_impl_clock_getcpuclockid2(                           \
      (long long)(idtype), (long long)(id), (long long)(clock_id))
#define __sanitizer_syscall_post_clock_getcpuclockid2(res, idtype, id,         \
                                                      clock_id)                \
  __sanitizer_syscall_post_impl_clock_getcpuclockid2(                          \
      res, (long long)(idtype), (long long)(id), (long long)(clock_id))

#ifdef __cplusplus
extern "C" {
#endif

// Private declarations. Do not call directly from user code. Use macros above.

// DO NOT EDIT! THIS FILE HAS BEEN GENERATED!

void __sanitizer_syscall_pre_impl_syscall(long long code, long long arg0,
                                          long long arg1, long long arg2,
                                          long long arg3, long long arg4,
                                          long long arg5, long long arg6,
                                          long long arg7);
void __sanitizer_syscall_post_impl_syscall(long long res, long long code,
                                           long long arg0, long long arg1,
                                           long long arg2, long long arg3,
                                           long long arg4, long long arg5,
                                           long long arg6, long long arg7);
void __sanitizer_syscall_pre_impl_exit(long long rval);
void __sanitizer_syscall_post_impl_exit(long long res, long long rval);
void __sanitizer_syscall_pre_impl_fork(void);
void __sanitizer_syscall_post_impl_fork(long long res);
void __sanitizer_syscall_pre_impl_read(long long fd, long long buf,
                                       long long nbyte);
void __sanitizer_syscall_post_impl_read(long long res, long long fd,
                                        long long buf, long long nbyte);
void __sanitizer_syscall_pre_impl_write(long long fd, long long buf,
                                        long long nbyte);
void __sanitizer_syscall_post_impl_write(long long res, long long fd,
                                         long long buf, long long nbyte);
void __sanitizer_syscall_pre_impl_open(long long path, long long flags,
                                       long long mode);
void __sanitizer_syscall_post_impl_open(long long res, long long path,
                                        long long flags, long long mode);
void __sanitizer_syscall_pre_impl_close(long long fd);
void __sanitizer_syscall_post_impl_close(long long res, long long fd);
void __sanitizer_syscall_pre_impl_compat_50_wait4(long long pid,
                                                  long long status,
                                                  long long options,
                                                  long long rusage);
void __sanitizer_syscall_post_impl_compat_50_wait4(long long res, long long pid,
                                                   long long status,
                                                   long long options,
                                                   long long rusage);
void __sanitizer_syscall_pre_impl_compat_43_ocreat(long long path,
                                                   long long mode);
void __sanitizer_syscall_post_impl_compat_43_ocreat(long long res,
                                                    long long path,
                                                    long long mode);
void __sanitizer_syscall_pre_impl_link(long long path, long long link);
void __sanitizer_syscall_post_impl_link(long long res, long long path,
                                        long long link);
void __sanitizer_syscall_pre_impl_unlink(long long path);
void __sanitizer_syscall_post_impl_unlink(long long res, long long path);
/* syscall 11 has been skipped */
void __sanitizer_syscall_pre_impl_chdir(long long path);
void __sanitizer_syscall_post_impl_chdir(long long res, long long path);
void __sanitizer_syscall_pre_impl_fchdir(long long fd);
void __sanitizer_syscall_post_impl_fchdir(long long res, long long fd);
void __sanitizer_syscall_pre_impl_compat_50_mknod(long long path,
                                                  long long mode,
                                                  long long dev);
void __sanitizer_syscall_post_impl_compat_50_mknod(long long res,
                                                   long long path,
                                                   long long mode,
                                                   long long dev);
void __sanitizer_syscall_pre_impl_chmod(long long path, long long mode);
void __sanitizer_syscall_post_impl_chmod(long long res, long long path,
                                         long long mode);
void __sanitizer_syscall_pre_impl_chown(long long path, long long uid,
                                        long long gid);
void __sanitizer_syscall_post_impl_chown(long long res, long long path,
                                         long long uid, long long gid);
void __sanitizer_syscall_pre_impl_break(long long nsize);
void __sanitizer_syscall_post_impl_break(long long res, long long nsize);
void __sanitizer_syscall_pre_impl_compat_20_getfsstat(long long buf,
                                                      long long bufsize,
                                                      long long flags);
void __sanitizer_syscall_post_impl_compat_20_getfsstat(long long res,
                                                       long long buf,
                                                       long long bufsize,
                                                       long long flags);
void __sanitizer_syscall_pre_impl_compat_43_olseek(long long fd,
                                                   long long offset,
                                                   long long whence);
void __sanitizer_syscall_post_impl_compat_43_olseek(long long res, long long fd,
                                                    long long offset,
                                                    long long whence);
void __sanitizer_syscall_pre_impl_getpid(void);
void __sanitizer_syscall_post_impl_getpid(long long res);
void __sanitizer_syscall_pre_impl_compat_40_mount(long long type,
                                                  long long path,
                                                  long long flags,
                                                  long long data);
void __sanitizer_syscall_post_impl_compat_40_mount(long long res,
                                                   long long type,
                                                   long long path,
                                                   long long flags,
                                                   long long data);
void __sanitizer_syscall_pre_impl_unmount(long long path, long long flags);
void __sanitizer_syscall_post_impl_unmount(long long res, long long path,
                                           long long flags);
void __sanitizer_syscall_pre_impl_setuid(long long uid);
void __sanitizer_syscall_post_impl_setuid(long long res, long long uid);
void __sanitizer_syscall_pre_impl_getuid(void);
void __sanitizer_syscall_post_impl_getuid(long long res);
void __sanitizer_syscall_pre_impl_geteuid(void);
void __sanitizer_syscall_post_impl_geteuid(long long res);
void __sanitizer_syscall_pre_impl_ptrace(long long req, long long pid,
                                         long long addr, long long data);
void __sanitizer_syscall_post_impl_ptrace(long long res, long long req,
                                          long long pid, long long addr,
                                          long long data);
void __sanitizer_syscall_pre_impl_recvmsg(long long s, long long msg,
                                          long long flags);
void __sanitizer_syscall_post_impl_recvmsg(long long res, long long s,
                                           long long msg, long long flags);
void __sanitizer_syscall_pre_impl_sendmsg(long long s, long long msg,
                                          long long flags);
void __sanitizer_syscall_post_impl_sendmsg(long long res, long long s,
                                           long long msg, long long flags);
void __sanitizer_syscall_pre_impl_recvfrom(long long s, long long buf,
                                           long long len, long long flags,
                                           long long from,
                                           long long fromlenaddr);
void __sanitizer_syscall_post_impl_recvfrom(long long res, long long s,
                                            long long buf, long long len,
                                            long long flags, long long from,
                                            long long fromlenaddr);
void __sanitizer_syscall_pre_impl_accept(long long s, long long name,
                                         long long anamelen);
void __sanitizer_syscall_post_impl_accept(long long res, long long s,
                                          long long name, long long anamelen);
void __sanitizer_syscall_pre_impl_getpeername(long long fdes, long long asa,
                                              long long alen);
void __sanitizer_syscall_post_impl_getpeername(long long res, long long fdes,
                                               long long asa, long long alen);
void __sanitizer_syscall_pre_impl_getsockname(long long fdes, long long asa,
                                              long long alen);
void __sanitizer_syscall_post_impl_getsockname(long long res, long long fdes,
                                               long long asa, long long alen);
void __sanitizer_syscall_pre_impl_access(long long path, long long flags);
void __sanitizer_syscall_post_impl_access(long long res, long long path,
                                          long long flags);
void __sanitizer_syscall_pre_impl_chflags(long long path, long long flags);
void __sanitizer_syscall_post_impl_chflags(long long res, long long path,
                                           long long flags);
void __sanitizer_syscall_pre_impl_fchflags(long long fd, long long flags);
void __sanitizer_syscall_post_impl_fchflags(long long res, long long fd,
                                            long long flags);
void __sanitizer_syscall_pre_impl_sync(void);
void __sanitizer_syscall_post_impl_sync(long long res);
void __sanitizer_syscall_pre_impl_kill(long long pid, long long signum);
void __sanitizer_syscall_post_impl_kill(long long res, long long pid,
                                        long long signum);
void __sanitizer_syscall_pre_impl_compat_43_stat43(long long path,
                                                   long long ub);
void __sanitizer_syscall_post_impl_compat_43_stat43(long long res,
                                                    long long path,
                                                    long long ub);
void __sanitizer_syscall_pre_impl_getppid(void);
void __sanitizer_syscall_post_impl_getppid(long long res);
void __sanitizer_syscall_pre_impl_compat_43_lstat43(long long path,
                                                    long long ub);
void __sanitizer_syscall_post_impl_compat_43_lstat43(long long res,
                                                     long long path,
                                                     long long ub);
void __sanitizer_syscall_pre_impl_dup(long long fd);
void __sanitizer_syscall_post_impl_dup(long long res, long long fd);
void __sanitizer_syscall_pre_impl_pipe(void);
void __sanitizer_syscall_post_impl_pipe(long long res);
void __sanitizer_syscall_pre_impl_getegid(void);
void __sanitizer_syscall_post_impl_getegid(long long res);
void __sanitizer_syscall_pre_impl_profil(long long samples, long long size,
                                         long long offset, long long scale);
void __sanitizer_syscall_post_impl_profil(long long res, long long samples,
                                          long long size, long long offset,
                                          long long scale);
void __sanitizer_syscall_pre_impl_ktrace(long long fname, long long ops,
                                         long long facs, long long pid);
void __sanitizer_syscall_post_impl_ktrace(long long res, long long fname,
                                          long long ops, long long facs,
                                          long long pid);
void __sanitizer_syscall_pre_impl_compat_13_sigaction13(long long signum,
                                                        long long nsa,
                                                        long long osa);
void __sanitizer_syscall_post_impl_compat_13_sigaction13(long long res,
                                                         long long signum,
                                                         long long nsa,
                                                         long long osa);
void __sanitizer_syscall_pre_impl_getgid(void);
void __sanitizer_syscall_post_impl_getgid(long long res);
void __sanitizer_syscall_pre_impl_compat_13_sigprocmask13(long long how,
                                                          long long mask);
void __sanitizer_syscall_post_impl_compat_13_sigprocmask13(long long res,
                                                           long long how,
                                                           long long mask);
void __sanitizer_syscall_pre_impl___getlogin(long long namebuf,
                                             long long namelen);
void __sanitizer_syscall_post_impl___getlogin(long long res, long long namebuf,
                                              long long namelen);
void __sanitizer_syscall_pre_impl___setlogin(long long namebuf);
void __sanitizer_syscall_post_impl___setlogin(long long res, long long namebuf);
void __sanitizer_syscall_pre_impl_acct(long long path);
void __sanitizer_syscall_post_impl_acct(long long res, long long path);
void __sanitizer_syscall_pre_impl_compat_13_sigpending13(void);
void __sanitizer_syscall_post_impl_compat_13_sigpending13(long long res);
void __sanitizer_syscall_pre_impl_compat_13_sigaltstack13(long long nss,
                                                          long long oss);
void __sanitizer_syscall_post_impl_compat_13_sigaltstack13(long long res,
                                                           long long nss,
                                                           long long oss);
void __sanitizer_syscall_pre_impl_ioctl(long long fd, long long com,
                                        long long data);
void __sanitizer_syscall_post_impl_ioctl(long long res, long long fd,
                                         long long com, long long data);
void __sanitizer_syscall_pre_impl_compat_12_oreboot(long long opt);
void __sanitizer_syscall_post_impl_compat_12_oreboot(long long res,
                                                     long long opt);
void __sanitizer_syscall_pre_impl_revoke(long long path);
void __sanitizer_syscall_post_impl_revoke(long long res, long long path);
void __sanitizer_syscall_pre_impl_symlink(long long path, long long link);
void __sanitizer_syscall_post_impl_symlink(long long res, long long path,
                                           long long link);
void __sanitizer_syscall_pre_impl_readlink(long long path, long long buf,
                                           long long count);
void __sanitizer_syscall_post_impl_readlink(long long res, long long path,
                                            long long buf, long long count);
void __sanitizer_syscall_pre_impl_execve(long long path, long long argp,
                                         long long envp);
void __sanitizer_syscall_post_impl_execve(long long res, long long path,
                                          long long argp, long long envp);
void __sanitizer_syscall_pre_impl_umask(long long newmask);
void __sanitizer_syscall_post_impl_umask(long long res, long long newmask);
void __sanitizer_syscall_pre_impl_chroot(long long path);
void __sanitizer_syscall_post_impl_chroot(long long res, long long path);
void __sanitizer_syscall_pre_impl_compat_43_fstat43(long long fd, long long sb);
void __sanitizer_syscall_post_impl_compat_43_fstat43(long long res,
                                                     long long fd,
                                                     long long sb);
void __sanitizer_syscall_pre_impl_compat_43_ogetkerninfo(long long op,
                                                         long long where,
                                                         long long size,
                                                         long long arg);
void __sanitizer_syscall_post_impl_compat_43_ogetkerninfo(long long res,
                                                          long long op,
                                                          long long where,
                                                          long long size,
                                                          long long arg);
void __sanitizer_syscall_pre_impl_compat_43_ogetpagesize(void);
void __sanitizer_syscall_post_impl_compat_43_ogetpagesize(long long res);
void __sanitizer_syscall_pre_impl_compat_12_msync(long long addr,
                                                  long long len);
void __sanitizer_syscall_post_impl_compat_12_msync(long long res,
                                                   long long addr,
                                                   long long len);
void __sanitizer_syscall_pre_impl_vfork(void);
void __sanitizer_syscall_post_impl_vfork(long long res);
/* syscall 67 has been skipped */
/* syscall 68 has been skipped */
/* syscall 69 has been skipped */
/* syscall 70 has been skipped */
void __sanitizer_syscall_pre_impl_compat_43_ommap(long long addr, long long len,
                                                  long long prot,
                                                  long long flags, long long fd,
                                                  long long pos);
void __sanitizer_syscall_post_impl_compat_43_ommap(
    long long res, long long addr, long long len, long long prot,
    long long flags, long long fd, long long pos);
void __sanitizer_syscall_pre_impl_vadvise(long long anom);
void __sanitizer_syscall_post_impl_vadvise(long long res, long long anom);
void __sanitizer_syscall_pre_impl_munmap(long long addr, long long len);
void __sanitizer_syscall_post_impl_munmap(long long res, long long addr,
                                          long long len);
void __sanitizer_syscall_pre_impl_mprotect(long long addr, long long len,
                                           long long prot);
void __sanitizer_syscall_post_impl_mprotect(long long res, long long addr,
                                            long long len, long long prot);
void __sanitizer_syscall_pre_impl_madvise(long long addr, long long len,
                                          long long behav);
void __sanitizer_syscall_post_impl_madvise(long long res, long long addr,
                                           long long len, long long behav);
/* syscall 76 has been skipped */
/* syscall 77 has been skipped */
void __sanitizer_syscall_pre_impl_mincore(long long addr, long long len,
                                          long long vec);
void __sanitizer_syscall_post_impl_mincore(long long res, long long addr,
                                           long long len, long long vec);
void __sanitizer_syscall_pre_impl_getgroups(long long gidsetsize,
                                            long long gidset);
void __sanitizer_syscall_post_impl_getgroups(long long res,
                                             long long gidsetsize,
                                             long long gidset);
void __sanitizer_syscall_pre_impl_setgroups(long long gidsetsize,
                                            long long gidset);
void __sanitizer_syscall_post_impl_setgroups(long long res,
                                             long long gidsetsize,
                                             long long gidset);
void __sanitizer_syscall_pre_impl_getpgrp(void);
void __sanitizer_syscall_post_impl_getpgrp(long long res);
void __sanitizer_syscall_pre_impl_setpgid(long long pid, long long pgid);
void __sanitizer_syscall_post_impl_setpgid(long long res, long long pid,
                                           long long pgid);
void __sanitizer_syscall_pre_impl_compat_50_setitimer(long long which,
                                                      long long itv,
                                                      long long oitv);
void __sanitizer_syscall_post_impl_compat_50_setitimer(long long res,
                                                       long long which,
                                                       long long itv,
                                                       long long oitv);
void __sanitizer_syscall_pre_impl_compat_43_owait(void);
void __sanitizer_syscall_post_impl_compat_43_owait(long long res);
void __sanitizer_syscall_pre_impl_compat_12_oswapon(long long name);
void __sanitizer_syscall_post_impl_compat_12_oswapon(long long res,
                                                     long long name);
void __sanitizer_syscall_pre_impl_compat_50_getitimer(long long which,
                                                      long long itv);
void __sanitizer_syscall_post_impl_compat_50_getitimer(long long res,
                                                       long long which,
                                                       long long itv);
void __sanitizer_syscall_pre_impl_compat_43_ogethostname(long long hostname,
                                                         long long len);
void __sanitizer_syscall_post_impl_compat_43_ogethostname(long long res,
                                                          long long hostname,
                                                          long long len);
void __sanitizer_syscall_pre_impl_compat_43_osethostname(long long hostname,
                                                         long long len);
void __sanitizer_syscall_post_impl_compat_43_osethostname(long long res,
                                                          long long hostname,
                                                          long long len);
void __sanitizer_syscall_pre_impl_compat_43_ogetdtablesize(void);
void __sanitizer_syscall_post_impl_compat_43_ogetdtablesize(long long res);
void __sanitizer_syscall_pre_impl_dup2(long long from, long long to);
void __sanitizer_syscall_post_impl_dup2(long long res, long long from,
                                        long long to);
/* syscall 91 has been skipped */
void __sanitizer_syscall_pre_impl_fcntl(long long fd, long long cmd,
                                        long long arg);
void __sanitizer_syscall_post_impl_fcntl(long long res, long long fd,
                                         long long cmd, long long arg);
void __sanitizer_syscall_pre_impl_compat_50_select(long long nd, long long in,
                                                   long long ou, long long ex,
                                                   long long tv);
void __sanitizer_syscall_post_impl_compat_50_select(long long res, long long nd,
                                                    long long in, long long ou,
                                                    long long ex, long long tv);
/* syscall 94 has been skipped */
void __sanitizer_syscall_pre_impl_fsync(long long fd);
void __sanitizer_syscall_post_impl_fsync(long long res, long long fd);
void __sanitizer_syscall_pre_impl_setpriority(long long which, long long who,
                                              long long prio);
void __sanitizer_syscall_post_impl_setpriority(long long res, long long which,
                                               long long who, long long prio);
void __sanitizer_syscall_pre_impl_compat_30_socket(long long domain,
                                                   long long type,
                                                   long long protocol);
void __sanitizer_syscall_post_impl_compat_30_socket(long long res,
                                                    long long domain,
                                                    long long type,
                                                    long long protocol);
void __sanitizer_syscall_pre_impl_connect(long long s, long long name,
                                          long long namelen);
void __sanitizer_syscall_post_impl_connect(long long res, long long s,
                                           long long name, long long namelen);
void __sanitizer_syscall_pre_impl_compat_43_oaccept(long long s, long long name,
                                                    long long anamelen);
void __sanitizer_syscall_post_impl_compat_43_oaccept(long long res, long long s,
                                                     long long name,
                                                     long long anamelen);
void __sanitizer_syscall_pre_impl_getpriority(long long which, long long who);
void __sanitizer_syscall_post_impl_getpriority(long long res, long long which,
                                               long long who);
void __sanitizer_syscall_pre_impl_compat_43_osend(long long s, long long buf,
                                                  long long len,
                                                  long long flags);
void __sanitizer_syscall_post_impl_compat_43_osend(long long res, long long s,
                                                   long long buf, long long len,
                                                   long long flags);
void __sanitizer_syscall_pre_impl_compat_43_orecv(long long s, long long buf,
                                                  long long len,
                                                  long long flags);
void __sanitizer_syscall_post_impl_compat_43_orecv(long long res, long long s,
                                                   long long buf, long long len,
                                                   long long flags);
void __sanitizer_syscall_pre_impl_compat_13_sigreturn13(long long sigcntxp);
void __sanitizer_syscall_post_impl_compat_13_sigreturn13(long long res,
                                                         long long sigcntxp);
void __sanitizer_syscall_pre_impl_bind(long long s, long long name,
                                       long long namelen);
void __sanitizer_syscall_post_impl_bind(long long res, long long s,
                                        long long name, long long namelen);
void __sanitizer_syscall_pre_impl_setsockopt(long long s, long long level,
                                             long long name, long long val,
                                             long long valsize);
void __sanitizer_syscall_post_impl_setsockopt(long long res, long long s,
                                              long long level, long long name,
                                              long long val, long long valsize);
void __sanitizer_syscall_pre_impl_listen(long long s, long long backlog);
void __sanitizer_syscall_post_impl_listen(long long res, long long s,
                                          long long backlog);
/* syscall 107 has been skipped */
void __sanitizer_syscall_pre_impl_compat_43_osigvec(long long signum,
                                                    long long nsv,
                                                    long long osv);
void __sanitizer_syscall_post_impl_compat_43_osigvec(long long res,
                                                     long long signum,
                                                     long long nsv,
                                                     long long osv);
void __sanitizer_syscall_pre_impl_compat_43_osigblock(long long mask);
void __sanitizer_syscall_post_impl_compat_43_osigblock(long long res,
                                                       long long mask);
void __sanitizer_syscall_pre_impl_compat_43_osigsetmask(long long mask);
void __sanitizer_syscall_post_impl_compat_43_osigsetmask(long long res,
                                                         long long mask);
void __sanitizer_syscall_pre_impl_compat_13_sigsuspend13(long long mask);
void __sanitizer_syscall_post_impl_compat_13_sigsuspend13(long long res,
                                                          long long mask);
void __sanitizer_syscall_pre_impl_compat_43_osigstack(long long nss,
                                                      long long oss);
void __sanitizer_syscall_post_impl_compat_43_osigstack(long long res,
                                                       long long nss,
                                                       long long oss);
void __sanitizer_syscall_pre_impl_compat_43_orecvmsg(long long s, long long msg,
                                                     long long flags);
void __sanitizer_syscall_post_impl_compat_43_orecvmsg(long long res,
                                                      long long s,
                                                      long long msg,
                                                      long long flags);
void __sanitizer_syscall_pre_impl_compat_43_osendmsg(long long s, long long msg,
                                                     long long flags);
void __sanitizer_syscall_post_impl_compat_43_osendmsg(long long res,
                                                      long long s,
                                                      long long msg,
                                                      long long flags);
/* syscall 115 has been skipped */
void __sanitizer_syscall_pre_impl_compat_50_gettimeofday(long long tp,
                                                         long long tzp);
void __sanitizer_syscall_post_impl_compat_50_gettimeofday(long long res,
                                                          long long tp,
                                                          long long tzp);
void __sanitizer_syscall_pre_impl_compat_50_getrusage(long long who,
                                                      long long rusage);
void __sanitizer_syscall_post_impl_compat_50_getrusage(long long res,
                                                       long long who,
                                                       long long rusage);
void __sanitizer_syscall_pre_impl_getsockopt(long long s, long long level,
                                             long long name, long long val,
                                             long long avalsize);
void __sanitizer_syscall_post_impl_getsockopt(long long res, long long s,
                                              long long level, long long name,
                                              long long val,
                                              long long avalsize);
/* syscall 119 has been skipped */
void __sanitizer_syscall_pre_impl_readv(long long fd, long long iovp,
                                        long long iovcnt);
void __sanitizer_syscall_post_impl_readv(long long res, long long fd,
                                         long long iovp, long long iovcnt);
void __sanitizer_syscall_pre_impl_writev(long long fd, long long iovp,
                                         long long iovcnt);
void __sanitizer_syscall_post_impl_writev(long long res, long long fd,
                                          long long iovp, long long iovcnt);
void __sanitizer_syscall_pre_impl_compat_50_settimeofday(long long tv,
                                                         long long tzp);
void __sanitizer_syscall_post_impl_compat_50_settimeofday(long long res,
                                                          long long tv,
                                                          long long tzp);
void __sanitizer_syscall_pre_impl_fchown(long long fd, long long uid,
                                         long long gid);
void __sanitizer_syscall_post_impl_fchown(long long res, long long fd,
                                          long long uid, long long gid);
void __sanitizer_syscall_pre_impl_fchmod(long long fd, long long mode);
void __sanitizer_syscall_post_impl_fchmod(long long res, long long fd,
                                          long long mode);
void __sanitizer_syscall_pre_impl_compat_43_orecvfrom(
    long long s, long long buf, long long len, long long flags, long long from,
    long long fromlenaddr);
void __sanitizer_syscall_post_impl_compat_43_orecvfrom(
    long long res, long long s, long long buf, long long len, long long flags,
    long long from, long long fromlenaddr);
void __sanitizer_syscall_pre_impl_setreuid(long long ruid, long long euid);
void __sanitizer_syscall_post_impl_setreuid(long long res, long long ruid,
                                            long long euid);
void __sanitizer_syscall_pre_impl_setregid(long long rgid, long long egid);
void __sanitizer_syscall_post_impl_setregid(long long res, long long rgid,
                                            long long egid);
void __sanitizer_syscall_pre_impl_rename(long long from, long long to);
void __sanitizer_syscall_post_impl_rename(long long res, long long from,
                                          long long to);
void __sanitizer_syscall_pre_impl_compat_43_otruncate(long long path,
                                                      long long length);
void __sanitizer_syscall_post_impl_compat_43_otruncate(long long res,
                                                       long long path,
                                                       long long length);
void __sanitizer_syscall_pre_impl_compat_43_oftruncate(long long fd,
                                                       long long length);
void __sanitizer_syscall_post_impl_compat_43_oftruncate(long long res,
                                                        long long fd,
                                                        long long length);
void __sanitizer_syscall_pre_impl_flock(long long fd, long long how);
void __sanitizer_syscall_post_impl_flock(long long res, long long fd,
                                         long long how);
void __sanitizer_syscall_pre_impl_mkfifo(long long path, long long mode);
void __sanitizer_syscall_post_impl_mkfifo(long long res, long long path,
                                          long long mode);
void __sanitizer_syscall_pre_impl_sendto(long long s, long long buf,
                                         long long len, long long flags,
                                         long long to, long long tolen);
void __sanitizer_syscall_post_impl_sendto(long long res, long long s,
                                          long long buf, long long len,
                                          long long flags, long long to,
                                          long long tolen);
void __sanitizer_syscall_pre_impl_shutdown(long long s, long long how);
void __sanitizer_syscall_post_impl_shutdown(long long res, long long s,
                                            long long how);
void __sanitizer_syscall_pre_impl_socketpair(long long domain, long long type,
                                             long long protocol, long long rsv);
void __sanitizer_syscall_post_impl_socketpair(long long res, long long domain,
                                              long long type,
                                              long long protocol,
                                              long long rsv);
void __sanitizer_syscall_pre_impl_mkdir(long long path, long long mode);
void __sanitizer_syscall_post_impl_mkdir(long long res, long long path,
                                         long long mode);
void __sanitizer_syscall_pre_impl_rmdir(long long path);
void __sanitizer_syscall_post_impl_rmdir(long long res, long long path);
void __sanitizer_syscall_pre_impl_compat_50_utimes(long long path,
                                                   long long tptr);
void __sanitizer_syscall_post_impl_compat_50_utimes(long long res,
                                                    long long path,
                                                    long long tptr);
/* syscall 139 has been skipped */
void __sanitizer_syscall_pre_impl_compat_50_adjtime(long long delta,
                                                    long long olddelta);
void __sanitizer_syscall_post_impl_compat_50_adjtime(long long res,
                                                     long long delta,
                                                     long long olddelta);
void __sanitizer_syscall_pre_impl_compat_43_ogetpeername(long long fdes,
                                                         long long asa,
                                                         long long alen);
void __sanitizer_syscall_post_impl_compat_43_ogetpeername(long long res,
                                                          long long fdes,
                                                          long long asa,
                                                          long long alen);
void __sanitizer_syscall_pre_impl_compat_43_ogethostid(void);
void __sanitizer_syscall_post_impl_compat_43_ogethostid(long long res);
void __sanitizer_syscall_pre_impl_compat_43_osethostid(long long hostid);
void __sanitizer_syscall_post_impl_compat_43_osethostid(long long res,
                                                        long long hostid);
void __sanitizer_syscall_pre_impl_compat_43_ogetrlimit(long long which,
                                                       long long rlp);
void __sanitizer_syscall_post_impl_compat_43_ogetrlimit(long long res,
                                                        long long which,
                                                        long long rlp);
void __sanitizer_syscall_pre_impl_compat_43_osetrlimit(long long which,
                                                       long long rlp);
void __sanitizer_syscall_post_impl_compat_43_osetrlimit(long long res,
                                                        long long which,
                                                        long long rlp);
void __sanitizer_syscall_pre_impl_compat_43_okillpg(long long pgid,
                                                    long long signum);
void __sanitizer_syscall_post_impl_compat_43_okillpg(long long res,
                                                     long long pgid,
                                                     long long signum);
void __sanitizer_syscall_pre_impl_setsid(void);
void __sanitizer_syscall_post_impl_setsid(long long res);
void __sanitizer_syscall_pre_impl_compat_50_quotactl(long long path,
                                                     long long cmd,
                                                     long long uid,
                                                     long long arg);
void __sanitizer_syscall_post_impl_compat_50_quotactl(
    long long res, long long path, long long cmd, long long uid, long long arg);
void __sanitizer_syscall_pre_impl_compat_43_oquota(void);
void __sanitizer_syscall_post_impl_compat_43_oquota(long long res);
void __sanitizer_syscall_pre_impl_compat_43_ogetsockname(long long fdec,
                                                         long long asa,
                                                         long long alen);
void __sanitizer_syscall_post_impl_compat_43_ogetsockname(long long res,
                                                          long long fdec,
                                                          long long asa,
                                                          long long alen);
/* syscall 151 has been skipped */
/* syscall 152 has been skipped */
/* syscall 153 has been skipped */
/* syscall 154 has been skipped */
void __sanitizer_syscall_pre_impl_nfssvc(long long flag, long long argp);
void __sanitizer_syscall_post_impl_nfssvc(long long res, long long flag,
                                          long long argp);
void __sanitizer_syscall_pre_impl_compat_43_ogetdirentries(long long fd,
                                                           long long buf,
                                                           long long count,
                                                           long long basep);
void __sanitizer_syscall_post_impl_compat_43_ogetdirentries(long long res,
                                                            long long fd,
                                                            long long buf,
                                                            long long count,
                                                            long long basep);
void __sanitizer_syscall_pre_impl_compat_20_statfs(long long path,
                                                   long long buf);
void __sanitizer_syscall_post_impl_compat_20_statfs(long long res,
                                                    long long path,
                                                    long long buf);
void __sanitizer_syscall_pre_impl_compat_20_fstatfs(long long fd,
                                                    long long buf);
void __sanitizer_syscall_post_impl_compat_20_fstatfs(long long res,
                                                     long long fd,
                                                     long long buf);
/* syscall 159 has been skipped */
/* syscall 160 has been skipped */
void __sanitizer_syscall_pre_impl_compat_30_getfh(long long fname,
                                                  long long fhp);
void __sanitizer_syscall_post_impl_compat_30_getfh(long long res,
                                                   long long fname,
                                                   long long fhp);
void __sanitizer_syscall_pre_impl_compat_09_ogetdomainname(long long domainname,
                                                           long long len);
void __sanitizer_syscall_post_impl_compat_09_ogetdomainname(
    long long res, long long domainname, long long len);
void __sanitizer_syscall_pre_impl_compat_09_osetdomainname(long long domainname,
                                                           long long len);
void __sanitizer_syscall_post_impl_compat_09_osetdomainname(
    long long res, long long domainname, long long len);
void __sanitizer_syscall_pre_impl_compat_09_ouname(long long name);
void __sanitizer_syscall_post_impl_compat_09_ouname(long long res,
                                                    long long name);
void __sanitizer_syscall_pre_impl_sysarch(long long op, long long parms);
void __sanitizer_syscall_post_impl_sysarch(long long res, long long op,
                                           long long parms);
/* syscall 166 has been skipped */
/* syscall 167 has been skipped */
/* syscall 168 has been skipped */
#if !defined(_LP64)
void __sanitizer_syscall_pre_impl_compat_10_osemsys(long long which,
                                                    long long a2, long long a3,
                                                    long long a4, long long a5);
void __sanitizer_syscall_post_impl_compat_10_osemsys(long long res,
                                                     long long which,
                                                     long long a2, long long a3,
                                                     long long a4,
                                                     long long a5);
#else
/* syscall 169 has been skipped */
#endif
#if !defined(_LP64)
void __sanitizer_syscall_pre_impl_compat_10_omsgsys(long long which,
                                                    long long a2, long long a3,
                                                    long long a4, long long a5,
                                                    long long a6);
void __sanitizer_syscall_post_impl_compat_10_omsgsys(long long res,
                                                     long long which,
                                                     long long a2, long long a3,
                                                     long long a4, long long a5,
                                                     long long a6);
#else
/* syscall 170 has been skipped */
#endif
#if !defined(_LP64)
void __sanitizer_syscall_pre_impl_compat_10_oshmsys(long long which,
                                                    long long a2, long long a3,
                                                    long long a4);
void __sanitizer_syscall_post_impl_compat_10_oshmsys(long long res,
                                                     long long which,
                                                     long long a2, long long a3,
                                                     long long a4);
#else
/* syscall 171 has been skipped */
#endif
/* syscall 172 has been skipped */
void __sanitizer_syscall_pre_impl_pread(long long fd, long long buf,
                                        long long nbyte, long long PAD,
                                        long long offset);
void __sanitizer_syscall_post_impl_pread(long long res, long long fd,
                                         long long buf, long long nbyte,
                                         long long PAD, long long offset);
void __sanitizer_syscall_pre_impl_pwrite(long long fd, long long buf,
                                         long long nbyte, long long PAD,
                                         long long offset);
void __sanitizer_syscall_post_impl_pwrite(long long res, long long fd,
                                          long long buf, long long nbyte,
                                          long long PAD, long long offset);
void __sanitizer_syscall_pre_impl_compat_30_ntp_gettime(long long ntvp);
void __sanitizer_syscall_post_impl_compat_30_ntp_gettime(long long res,
                                                         long long ntvp);
#if defined(NTP) || !defined(_KERNEL_OPT)
void __sanitizer_syscall_pre_impl_ntp_adjtime(long long tp);
void __sanitizer_syscall_post_impl_ntp_adjtime(long long res, long long tp);
#else
/* syscall 176 has been skipped */
#endif
/* syscall 177 has been skipped */
/* syscall 178 has been skipped */
/* syscall 179 has been skipped */
/* syscall 180 has been skipped */
void __sanitizer_syscall_pre_impl_setgid(long long gid);
void __sanitizer_syscall_post_impl_setgid(long long res, long long gid);
void __sanitizer_syscall_pre_impl_setegid(long long egid);
void __sanitizer_syscall_post_impl_setegid(long long res, long long egid);
void __sanitizer_syscall_pre_impl_seteuid(long long euid);
void __sanitizer_syscall_post_impl_seteuid(long long res, long long euid);
void __sanitizer_syscall_pre_impl_lfs_bmapv(long long fsidp, long long blkiov,
                                            long long blkcnt);
void __sanitizer_syscall_post_impl_lfs_bmapv(long long res, long long fsidp,
                                             long long blkiov,
                                             long long blkcnt);
void __sanitizer_syscall_pre_impl_lfs_markv(long long fsidp, long long blkiov,
                                            long long blkcnt);
void __sanitizer_syscall_post_impl_lfs_markv(long long res, long long fsidp,
                                             long long blkiov,
                                             long long blkcnt);
void __sanitizer_syscall_pre_impl_lfs_segclean(long long fsidp,
                                               long long segment);
void __sanitizer_syscall_post_impl_lfs_segclean(long long res, long long fsidp,
                                                long long segment);
void __sanitizer_syscall_pre_impl_compat_50_lfs_segwait(long long fsidp,
                                                        long long tv);
void __sanitizer_syscall_post_impl_compat_50_lfs_segwait(long long res,
                                                         long long fsidp,
                                                         long long tv);
void __sanitizer_syscall_pre_impl_compat_12_stat12(long long path,
                                                   long long ub);
void __sanitizer_syscall_post_impl_compat_12_stat12(long long res,
                                                    long long path,
                                                    long long ub);
void __sanitizer_syscall_pre_impl_compat_12_fstat12(long long fd, long long sb);
void __sanitizer_syscall_post_impl_compat_12_fstat12(long long res,
                                                     long long fd,
                                                     long long sb);
void __sanitizer_syscall_pre_impl_compat_12_lstat12(long long path,
                                                    long long ub);
void __sanitizer_syscall_post_impl_compat_12_lstat12(long long res,
                                                     long long path,
                                                     long long ub);
void __sanitizer_syscall_pre_impl_pathconf(long long path, long long name);
void __sanitizer_syscall_post_impl_pathconf(long long res, long long path,
                                            long long name);
void __sanitizer_syscall_pre_impl_fpathconf(long long fd, long long name);
void __sanitizer_syscall_post_impl_fpathconf(long long res, long long fd,
                                             long long name);
void __sanitizer_syscall_pre_impl_getsockopt2(long long s, long long level,
                                              long long name, long long val,
                                              long long avalsize);
void __sanitizer_syscall_post_impl_getsockopt2(long long res, long long s,
                                               long long level, long long name,
                                               long long val,
                                               long long avalsize);
void __sanitizer_syscall_pre_impl_getrlimit(long long which, long long rlp);
void __sanitizer_syscall_post_impl_getrlimit(long long res, long long which,
                                             long long rlp);
void __sanitizer_syscall_pre_impl_setrlimit(long long which, long long rlp);
void __sanitizer_syscall_post_impl_setrlimit(long long res, long long which,
                                             long long rlp);
void __sanitizer_syscall_pre_impl_compat_12_getdirentries(long long fd,
                                                          long long buf,
                                                          long long count,
                                                          long long basep);
void __sanitizer_syscall_post_impl_compat_12_getdirentries(long long res,
                                                           long long fd,
                                                           long long buf,
                                                           long long count,
                                                           long long basep);
void __sanitizer_syscall_pre_impl_mmap(long long addr, long long len,
                                       long long prot, long long flags,
                                       long long fd, long long PAD,
                                       long long pos);
void __sanitizer_syscall_post_impl_mmap(long long res, long long addr,
                                        long long len, long long prot,
                                        long long flags, long long fd,
                                        long long PAD, long long pos);
void __sanitizer_syscall_pre_impl___syscall(long long code, long long arg0,
                                            long long arg1, long long arg2,
                                            long long arg3, long long arg4,
                                            long long arg5, long long arg6,
                                            long long arg7);
void __sanitizer_syscall_post_impl___syscall(long long res, long long code,
                                             long long arg0, long long arg1,
                                             long long arg2, long long arg3,
                                             long long arg4, long long arg5,
                                             long long arg6, long long arg7);
void __sanitizer_syscall_pre_impl_lseek(long long fd, long long PAD,
                                        long long offset, long long whence);
void __sanitizer_syscall_post_impl_lseek(long long res, long long fd,
                                         long long PAD, long long offset,
                                         long long whence);
void __sanitizer_syscall_pre_impl_truncate(long long path, long long PAD,
                                           long long length);
void __sanitizer_syscall_post_impl_truncate(long long res, long long path,
                                            long long PAD, long long length);
void __sanitizer_syscall_pre_impl_ftruncate(long long fd, long long PAD,
                                            long long length);
void __sanitizer_syscall_post_impl_ftruncate(long long res, long long fd,
                                             long long PAD, long long length);
void __sanitizer_syscall_pre_impl___sysctl(long long name, long long namelen,
                                           long long oldv, long long oldlenp,
                                           long long newv, long long newlen);
void __sanitizer_syscall_post_impl___sysctl(long long res, long long name,
                                            long long namelen, long long oldv,
                                            long long oldlenp, long long newv,
                                            long long newlen);
void __sanitizer_syscall_pre_impl_mlock(long long addr, long long len);
void __sanitizer_syscall_post_impl_mlock(long long res, long long addr,
                                         long long len);
void __sanitizer_syscall_pre_impl_munlock(long long addr, long long len);
void __sanitizer_syscall_post_impl_munlock(long long res, long long addr,
                                           long long len);
void __sanitizer_syscall_pre_impl_undelete(long long path);
void __sanitizer_syscall_post_impl_undelete(long long res, long long path);
void __sanitizer_syscall_pre_impl_compat_50_futimes(long long fd,
                                                    long long tptr);
void __sanitizer_syscall_post_impl_compat_50_futimes(long long res,
                                                     long long fd,
                                                     long long tptr);
void __sanitizer_syscall_pre_impl_getpgid(long long pid);
void __sanitizer_syscall_post_impl_getpgid(long long res, long long pid);
void __sanitizer_syscall_pre_impl_reboot(long long opt, long long bootstr);
void __sanitizer_syscall_post_impl_reboot(long long res, long long opt,
                                          long long bootstr);
void __sanitizer_syscall_pre_impl_poll(long long fds, long long nfds,
                                       long long timeout);
void __sanitizer_syscall_post_impl_poll(long long res, long long fds,
                                        long long nfds, long long timeout);
void __sanitizer_syscall_pre_impl_afssys(long long id, long long a1,
                                         long long a2, long long a3,
                                         long long a4, long long a5,
                                         long long a6);
void __sanitizer_syscall_post_impl_afssys(long long res, long long id,
                                          long long a1, long long a2,
                                          long long a3, long long a4,
                                          long long a5, long long a6);
/* syscall 211 has been skipped */
/* syscall 212 has been skipped */
/* syscall 213 has been skipped */
/* syscall 214 has been skipped */
/* syscall 215 has been skipped */
/* syscall 216 has been skipped */
/* syscall 217 has been skipped */
/* syscall 218 has been skipped */
/* syscall 219 has been skipped */
void __sanitizer_syscall_pre_impl_compat_14___semctl(long long semid,
                                                     long long semnum,
                                                     long long cmd,
                                                     long long arg);
void __sanitizer_syscall_post_impl_compat_14___semctl(long long res,
                                                      long long semid,
                                                      long long semnum,
                                                      long long cmd,
                                                      long long arg);
void __sanitizer_syscall_pre_impl_semget(long long key, long long nsems,
                                         long long semflg);
void __sanitizer_syscall_post_impl_semget(long long res, long long key,
                                          long long nsems, long long semflg);
void __sanitizer_syscall_pre_impl_semop(long long semid, long long sops,
                                        long long nsops);
void __sanitizer_syscall_post_impl_semop(long long res, long long semid,
                                         long long sops, long long nsops);
void __sanitizer_syscall_pre_impl_semconfig(long long flag);
void __sanitizer_syscall_post_impl_semconfig(long long res, long long flag);
void __sanitizer_syscall_pre_impl_compat_14_msgctl(long long msqid,
                                                   long long cmd,
                                                   long long buf);
void __sanitizer_syscall_post_impl_compat_14_msgctl(long long res,
                                                    long long msqid,
                                                    long long cmd,
                                                    long long buf);
void __sanitizer_syscall_pre_impl_msgget(long long key, long long msgflg);
void __sanitizer_syscall_post_impl_msgget(long long res, long long key,
                                          long long msgflg);
void __sanitizer_syscall_pre_impl_msgsnd(long long msqid, long long msgp,
                                         long long msgsz, long long msgflg);
void __sanitizer_syscall_post_impl_msgsnd(long long res, long long msqid,
                                          long long msgp, long long msgsz,
                                          long long msgflg);
void __sanitizer_syscall_pre_impl_msgrcv(long long msqid, long long msgp,
                                         long long msgsz, long long msgtyp,
                                         long long msgflg);
void __sanitizer_syscall_post_impl_msgrcv(long long res, long long msqid,
                                          long long msgp, long long msgsz,
                                          long long msgtyp, long long msgflg);
void __sanitizer_syscall_pre_impl_shmat(long long shmid, long long shmaddr,
                                        long long shmflg);
void __sanitizer_syscall_post_impl_shmat(long long res, long long shmid,
                                         long long shmaddr, long long shmflg);
void __sanitizer_syscall_pre_impl_compat_14_shmctl(long long shmid,
                                                   long long cmd,
                                                   long long buf);
void __sanitizer_syscall_post_impl_compat_14_shmctl(long long res,
                                                    long long shmid,
                                                    long long cmd,
                                                    long long buf);
void __sanitizer_syscall_pre_impl_shmdt(long long shmaddr);
void __sanitizer_syscall_post_impl_shmdt(long long res, long long shmaddr);
void __sanitizer_syscall_pre_impl_shmget(long long key, long long size,
                                         long long shmflg);
void __sanitizer_syscall_post_impl_shmget(long long res, long long key,
                                          long long size, long long shmflg);
void __sanitizer_syscall_pre_impl_compat_50_clock_gettime(long long clock_id,
                                                          long long tp);
void __sanitizer_syscall_post_impl_compat_50_clock_gettime(long long res,
                                                           long long clock_id,
                                                           long long tp);
void __sanitizer_syscall_pre_impl_compat_50_clock_settime(long long clock_id,
                                                          long long tp);
void __sanitizer_syscall_post_impl_compat_50_clock_settime(long long res,
                                                           long long clock_id,
                                                           long long tp);
void __sanitizer_syscall_pre_impl_compat_50_clock_getres(long long clock_id,
                                                         long long tp);
void __sanitizer_syscall_post_impl_compat_50_clock_getres(long long res,
                                                          long long clock_id,
                                                          long long tp);
void __sanitizer_syscall_pre_impl_timer_create(long long clock_id,
                                               long long evp,
                                               long long timerid);
void __sanitizer_syscall_post_impl_timer_create(long long res,
                                                long long clock_id,
                                                long long evp,
                                                long long timerid);
void __sanitizer_syscall_pre_impl_timer_delete(long long timerid);
void __sanitizer_syscall_post_impl_timer_delete(long long res,
                                                long long timerid);
void __sanitizer_syscall_pre_impl_compat_50_timer_settime(long long timerid,
                                                          long long flags,
                                                          long long value,
                                                          long long ovalue);
void __sanitizer_syscall_post_impl_compat_50_timer_settime(long long res,
                                                           long long timerid,
                                                           long long flags,
                                                           long long value,
                                                           long long ovalue);
void __sanitizer_syscall_pre_impl_compat_50_timer_gettime(long long timerid,
                                                          long long value);
void __sanitizer_syscall_post_impl_compat_50_timer_gettime(long long res,
                                                           long long timerid,
                                                           long long value);
void __sanitizer_syscall_pre_impl_timer_getoverrun(long long timerid);
void __sanitizer_syscall_post_impl_timer_getoverrun(long long res,
                                                    long long timerid);
void __sanitizer_syscall_pre_impl_compat_50_nanosleep(long long rqtp,
                                                      long long rmtp);
void __sanitizer_syscall_post_impl_compat_50_nanosleep(long long res,
                                                       long long rqtp,
                                                       long long rmtp);
void __sanitizer_syscall_pre_impl_fdatasync(long long fd);
void __sanitizer_syscall_post_impl_fdatasync(long long res, long long fd);
void __sanitizer_syscall_pre_impl_mlockall(long long flags);
void __sanitizer_syscall_post_impl_mlockall(long long res, long long flags);
void __sanitizer_syscall_pre_impl_munlockall(void);
void __sanitizer_syscall_post_impl_munlockall(long long res);
void __sanitizer_syscall_pre_impl_compat_50___sigtimedwait(long long set,
                                                           long long info,
                                                           long long timeout);
void __sanitizer_syscall_post_impl_compat_50___sigtimedwait(long long res,
                                                            long long set,
                                                            long long info,
                                                            long long timeout);
void __sanitizer_syscall_pre_impl_sigqueueinfo(long long pid, long long info);
void __sanitizer_syscall_post_impl_sigqueueinfo(long long res, long long pid,
                                                long long info);
void __sanitizer_syscall_pre_impl_modctl(long long cmd, long long arg);
void __sanitizer_syscall_post_impl_modctl(long long res, long long cmd,
                                          long long arg);
void __sanitizer_syscall_pre_impl__ksem_init(long long value, long long idp);
void __sanitizer_syscall_post_impl__ksem_init(long long res, long long value,
                                              long long idp);
void __sanitizer_syscall_pre_impl__ksem_open(long long name, long long oflag,
                                             long long mode, long long value,
                                             long long idp);
void __sanitizer_syscall_post_impl__ksem_open(long long res, long long name,
                                              long long oflag, long long mode,
                                              long long value, long long idp);
void __sanitizer_syscall_pre_impl__ksem_unlink(long long name);
void __sanitizer_syscall_post_impl__ksem_unlink(long long res, long long name);
void __sanitizer_syscall_pre_impl__ksem_close(long long id);
void __sanitizer_syscall_post_impl__ksem_close(long long res, long long id);
void __sanitizer_syscall_pre_impl__ksem_post(long long id);
void __sanitizer_syscall_post_impl__ksem_post(long long res, long long id);
void __sanitizer_syscall_pre_impl__ksem_wait(long long id);
void __sanitizer_syscall_post_impl__ksem_wait(long long res, long long id);
void __sanitizer_syscall_pre_impl__ksem_trywait(long long id);
void __sanitizer_syscall_post_impl__ksem_trywait(long long res, long long id);
void __sanitizer_syscall_pre_impl__ksem_getvalue(long long id, long long value);
void __sanitizer_syscall_post_impl__ksem_getvalue(long long res, long long id,
                                                  long long value);
void __sanitizer_syscall_pre_impl__ksem_destroy(long long id);
void __sanitizer_syscall_post_impl__ksem_destroy(long long res, long long id);
void __sanitizer_syscall_pre_impl__ksem_timedwait(long long id,
                                                  long long abstime);
void __sanitizer_syscall_post_impl__ksem_timedwait(long long res, long long id,
                                                   long long abstime);
void __sanitizer_syscall_pre_impl_mq_open(long long name, long long oflag,
                                          long long mode, long long attr);
void __sanitizer_syscall_post_impl_mq_open(long long res, long long name,
                                           long long oflag, long long mode,
                                           long long attr);
void __sanitizer_syscall_pre_impl_mq_close(long long mqdes);
void __sanitizer_syscall_post_impl_mq_close(long long res, long long mqdes);
void __sanitizer_syscall_pre_impl_mq_unlink(long long name);
void __sanitizer_syscall_post_impl_mq_unlink(long long res, long long name);
void __sanitizer_syscall_pre_impl_mq_getattr(long long mqdes, long long mqstat);
void __sanitizer_syscall_post_impl_mq_getattr(long long res, long long mqdes,
                                              long long mqstat);
void __sanitizer_syscall_pre_impl_mq_setattr(long long mqdes, long long mqstat,
                                             long long omqstat);
void __sanitizer_syscall_post_impl_mq_setattr(long long res, long long mqdes,
                                              long long mqstat,
                                              long long omqstat);
void __sanitizer_syscall_pre_impl_mq_notify(long long mqdes,
                                            long long notification);
void __sanitizer_syscall_post_impl_mq_notify(long long res, long long mqdes,
                                             long long notification);
void __sanitizer_syscall_pre_impl_mq_send(long long mqdes, long long msg_ptr,
                                          long long msg_len,
                                          long long msg_prio);
void __sanitizer_syscall_post_impl_mq_send(long long res, long long mqdes,
                                           long long msg_ptr, long long msg_len,
                                           long long msg_prio);
void __sanitizer_syscall_pre_impl_mq_receive(long long mqdes, long long msg_ptr,
                                             long long msg_len,
                                             long long msg_prio);
void __sanitizer_syscall_post_impl_mq_receive(long long res, long long mqdes,
                                              long long msg_ptr,
                                              long long msg_len,
                                              long long msg_prio);
void __sanitizer_syscall_pre_impl_compat_50_mq_timedsend(long long mqdes,
                                                         long long msg_ptr,
                                                         long long msg_len,
                                                         long long msg_prio,
                                                         long long abs_timeout);
void __sanitizer_syscall_post_impl_compat_50_mq_timedsend(
    long long res, long long mqdes, long long msg_ptr, long long msg_len,
    long long msg_prio, long long abs_timeout);
void __sanitizer_syscall_pre_impl_compat_50_mq_timedreceive(
    long long mqdes, long long msg_ptr, long long msg_len, long long msg_prio,
    long long abs_timeout);
void __sanitizer_syscall_post_impl_compat_50_mq_timedreceive(
    long long res, long long mqdes, long long msg_ptr, long long msg_len,
    long long msg_prio, long long abs_timeout);
/* syscall 267 has been skipped */
/* syscall 268 has been skipped */
/* syscall 269 has been skipped */
void __sanitizer_syscall_pre_impl___posix_rename(long long from, long long to);
void __sanitizer_syscall_post_impl___posix_rename(long long res, long long from,
                                                  long long to);
void __sanitizer_syscall_pre_impl_swapctl(long long cmd, long long arg,
                                          long long misc);
void __sanitizer_syscall_post_impl_swapctl(long long res, long long cmd,
                                           long long arg, long long misc);
void __sanitizer_syscall_pre_impl_compat_30_getdents(long long fd,
                                                     long long buf,
                                                     long long count);
void __sanitizer_syscall_post_impl_compat_30_getdents(long long res,
                                                      long long fd,
                                                      long long buf,
                                                      long long count);
void __sanitizer_syscall_pre_impl_minherit(long long addr, long long len,
                                           long long inherit);
void __sanitizer_syscall_post_impl_minherit(long long res, long long addr,
                                            long long len, long long inherit);
void __sanitizer_syscall_pre_impl_lchmod(long long path, long long mode);
void __sanitizer_syscall_post_impl_lchmod(long long res, long long path,
                                          long long mode);
void __sanitizer_syscall_pre_impl_lchown(long long path, long long uid,
                                         long long gid);
void __sanitizer_syscall_post_impl_lchown(long long res, long long path,
                                          long long uid, long long gid);
void __sanitizer_syscall_pre_impl_compat_50_lutimes(long long path,
                                                    long long tptr);
void __sanitizer_syscall_post_impl_compat_50_lutimes(long long res,
                                                     long long path,
                                                     long long tptr);
void __sanitizer_syscall_pre_impl___msync13(long long addr, long long len,
                                            long long flags);
void __sanitizer_syscall_post_impl___msync13(long long res, long long addr,
                                             long long len, long long flags);
void __sanitizer_syscall_pre_impl_compat_30___stat13(long long path,
                                                     long long ub);
void __sanitizer_syscall_post_impl_compat_30___stat13(long long res,
                                                      long long path,
                                                      long long ub);
void __sanitizer_syscall_pre_impl_compat_30___fstat13(long long fd,
                                                      long long sb);
void __sanitizer_syscall_post_impl_compat_30___fstat13(long long res,
                                                       long long fd,
                                                       long long sb);
void __sanitizer_syscall_pre_impl_compat_30___lstat13(long long path,
                                                      long long ub);
void __sanitizer_syscall_post_impl_compat_30___lstat13(long long res,
                                                       long long path,
                                                       long long ub);
void __sanitizer_syscall_pre_impl___sigaltstack14(long long nss, long long oss);
void __sanitizer_syscall_post_impl___sigaltstack14(long long res, long long nss,
                                                   long long oss);
void __sanitizer_syscall_pre_impl___vfork14(void);
void __sanitizer_syscall_post_impl___vfork14(long long res);
void __sanitizer_syscall_pre_impl___posix_chown(long long path, long long uid,
                                                long long gid);
void __sanitizer_syscall_post_impl___posix_chown(long long res, long long path,
                                                 long long uid, long long gid);
void __sanitizer_syscall_pre_impl___posix_fchown(long long fd, long long uid,
                                                 long long gid);
void __sanitizer_syscall_post_impl___posix_fchown(long long res, long long fd,
                                                  long long uid, long long gid);
void __sanitizer_syscall_pre_impl___posix_lchown(long long path, long long uid,
                                                 long long gid);
void __sanitizer_syscall_post_impl___posix_lchown(long long res, long long path,
                                                  long long uid, long long gid);
void __sanitizer_syscall_pre_impl_getsid(long long pid);
void __sanitizer_syscall_post_impl_getsid(long long res, long long pid);
void __sanitizer_syscall_pre_impl___clone(long long flags, long long stack);
void __sanitizer_syscall_post_impl___clone(long long res, long long flags,
                                           long long stack);
void __sanitizer_syscall_pre_impl_fktrace(long long fd, long long ops,
                                          long long facs, long long pid);
void __sanitizer_syscall_post_impl_fktrace(long long res, long long fd,
                                           long long ops, long long facs,
                                           long long pid);
void __sanitizer_syscall_pre_impl_preadv(long long fd, long long iovp,
                                         long long iovcnt, long long PAD,
                                         long long offset);
void __sanitizer_syscall_post_impl_preadv(long long res, long long fd,
                                          long long iovp, long long iovcnt,
                                          long long PAD, long long offset);
void __sanitizer_syscall_pre_impl_pwritev(long long fd, long long iovp,
                                          long long iovcnt, long long PAD,
                                          long long offset);
void __sanitizer_syscall_post_impl_pwritev(long long res, long long fd,
                                           long long iovp, long long iovcnt,
                                           long long PAD, long long offset);
void __sanitizer_syscall_pre_impl_compat_16___sigaction14(long long signum,
                                                          long long nsa,
                                                          long long osa);
void __sanitizer_syscall_post_impl_compat_16___sigaction14(long long res,
                                                           long long signum,
                                                           long long nsa,
                                                           long long osa);
void __sanitizer_syscall_pre_impl___sigpending14(long long set);
void __sanitizer_syscall_post_impl___sigpending14(long long res, long long set);
void __sanitizer_syscall_pre_impl___sigprocmask14(long long how, long long set,
                                                  long long oset);
void __sanitizer_syscall_post_impl___sigprocmask14(long long res, long long how,
                                                   long long set,
                                                   long long oset);
void __sanitizer_syscall_pre_impl___sigsuspend14(long long set);
void __sanitizer_syscall_post_impl___sigsuspend14(long long res, long long set);
void __sanitizer_syscall_pre_impl_compat_16___sigreturn14(long long sigcntxp);
void __sanitizer_syscall_post_impl_compat_16___sigreturn14(long long res,
                                                           long long sigcntxp);
void __sanitizer_syscall_pre_impl___getcwd(long long bufp, long long length);
void __sanitizer_syscall_post_impl___getcwd(long long res, long long bufp,
                                            long long length);
void __sanitizer_syscall_pre_impl_fchroot(long long fd);
void __sanitizer_syscall_post_impl_fchroot(long long res, long long fd);
void __sanitizer_syscall_pre_impl_compat_30_fhopen(long long fhp,
                                                   long long flags);
void __sanitizer_syscall_post_impl_compat_30_fhopen(long long res,
                                                    long long fhp,
                                                    long long flags);
void __sanitizer_syscall_pre_impl_compat_30_fhstat(long long fhp, long long sb);
void __sanitizer_syscall_post_impl_compat_30_fhstat(long long res,
                                                    long long fhp,
                                                    long long sb);
void __sanitizer_syscall_pre_impl_compat_20_fhstatfs(long long fhp,
                                                     long long buf);
void __sanitizer_syscall_post_impl_compat_20_fhstatfs(long long res,
                                                      long long fhp,
                                                      long long buf);
void __sanitizer_syscall_pre_impl_compat_50_____semctl13(long long semid,
                                                         long long semnum,
                                                         long long cmd,
                                                         long long arg);
void __sanitizer_syscall_post_impl_compat_50_____semctl13(long long res,
                                                          long long semid,
                                                          long long semnum,
                                                          long long cmd,
                                                          long long arg);
void __sanitizer_syscall_pre_impl_compat_50___msgctl13(long long msqid,
                                                       long long cmd,
                                                       long long buf);
void __sanitizer_syscall_post_impl_compat_50___msgctl13(long long res,
                                                        long long msqid,
                                                        long long cmd,
                                                        long long buf);
void __sanitizer_syscall_pre_impl_compat_50___shmctl13(long long shmid,
                                                       long long cmd,
                                                       long long buf);
void __sanitizer_syscall_post_impl_compat_50___shmctl13(long long res,
                                                        long long shmid,
                                                        long long cmd,
                                                        long long buf);
void __sanitizer_syscall_pre_impl_lchflags(long long path, long long flags);
void __sanitizer_syscall_post_impl_lchflags(long long res, long long path,
                                            long long flags);
void __sanitizer_syscall_pre_impl_issetugid(void);
void __sanitizer_syscall_post_impl_issetugid(long long res);
void __sanitizer_syscall_pre_impl_utrace(long long label, long long addr,
                                         long long len);
void __sanitizer_syscall_post_impl_utrace(long long res, long long label,
                                          long long addr, long long len);
void __sanitizer_syscall_pre_impl_getcontext(long long ucp);
void __sanitizer_syscall_post_impl_getcontext(long long res, long long ucp);
void __sanitizer_syscall_pre_impl_setcontext(long long ucp);
void __sanitizer_syscall_post_impl_setcontext(long long res, long long ucp);
void __sanitizer_syscall_pre_impl__lwp_create(long long ucp, long long flags,
                                              long long new_lwp);
void __sanitizer_syscall_post_impl__lwp_create(long long res, long long ucp,
                                               long long flags,
                                               long long new_lwp);
void __sanitizer_syscall_pre_impl__lwp_exit(void);
void __sanitizer_syscall_post_impl__lwp_exit(long long res);
void __sanitizer_syscall_pre_impl__lwp_self(void);
void __sanitizer_syscall_post_impl__lwp_self(long long res);
void __sanitizer_syscall_pre_impl__lwp_wait(long long wait_for,
                                            long long departed);
void __sanitizer_syscall_post_impl__lwp_wait(long long res, long long wait_for,
                                             long long departed);
void __sanitizer_syscall_pre_impl__lwp_suspend(long long target);
void __sanitizer_syscall_post_impl__lwp_suspend(long long res,
                                                long long target);
void __sanitizer_syscall_pre_impl__lwp_continue(long long target);
void __sanitizer_syscall_post_impl__lwp_continue(long long res,
                                                 long long target);
void __sanitizer_syscall_pre_impl__lwp_wakeup(long long target);
void __sanitizer_syscall_post_impl__lwp_wakeup(long long res, long long target);
void __sanitizer_syscall_pre_impl__lwp_getprivate(void);
void __sanitizer_syscall_post_impl__lwp_getprivate(long long res);
void __sanitizer_syscall_pre_impl__lwp_setprivate(long long ptr);
void __sanitizer_syscall_post_impl__lwp_setprivate(long long res,
                                                   long long ptr);
void __sanitizer_syscall_pre_impl__lwp_kill(long long target, long long signo);
void __sanitizer_syscall_post_impl__lwp_kill(long long res, long long target,
                                             long long signo);
void __sanitizer_syscall_pre_impl__lwp_detach(long long target);
void __sanitizer_syscall_post_impl__lwp_detach(long long res, long long target);
void __sanitizer_syscall_pre_impl_compat_50__lwp_park(long long ts,
                                                      long long unpark,
                                                      long long hint,
                                                      long long unparkhint);
void __sanitizer_syscall_post_impl_compat_50__lwp_park(long long res,
                                                       long long ts,
                                                       long long unpark,
                                                       long long hint,
                                                       long long unparkhint);
void __sanitizer_syscall_pre_impl__lwp_unpark(long long target, long long hint);
void __sanitizer_syscall_post_impl__lwp_unpark(long long res, long long target,
                                               long long hint);
void __sanitizer_syscall_pre_impl__lwp_unpark_all(long long targets,
                                                  long long ntargets,
                                                  long long hint);
void __sanitizer_syscall_post_impl__lwp_unpark_all(long long res,
                                                   long long targets,
                                                   long long ntargets,
                                                   long long hint);
void __sanitizer_syscall_pre_impl__lwp_setname(long long target,
                                               long long name);
void __sanitizer_syscall_post_impl__lwp_setname(long long res, long long target,
                                                long long name);
void __sanitizer_syscall_pre_impl__lwp_getname(long long target, long long name,
                                               long long len);
void __sanitizer_syscall_post_impl__lwp_getname(long long res, long long target,
                                                long long name, long long len);
void __sanitizer_syscall_pre_impl__lwp_ctl(long long features,
                                           long long address);
void __sanitizer_syscall_post_impl__lwp_ctl(long long res, long long features,
                                            long long address);
/* syscall 326 has been skipped */
/* syscall 327 has been skipped */
/* syscall 328 has been skipped */
/* syscall 329 has been skipped */
void __sanitizer_syscall_pre_impl_compat_60_sa_register(
    long long newv, long long oldv, long long flags,
    long long stackinfo_offset);
void __sanitizer_syscall_post_impl_compat_60_sa_register(
    long long res, long long newv, long long oldv, long long flags,
    long long stackinfo_offset);
void __sanitizer_syscall_pre_impl_compat_60_sa_stacks(long long num,
                                                      long long stacks);
void __sanitizer_syscall_post_impl_compat_60_sa_stacks(long long res,
                                                       long long num,
                                                       long long stacks);
void __sanitizer_syscall_pre_impl_compat_60_sa_enable(void);
void __sanitizer_syscall_post_impl_compat_60_sa_enable(long long res);
void __sanitizer_syscall_pre_impl_compat_60_sa_setconcurrency(
    long long concurrency);
void __sanitizer_syscall_post_impl_compat_60_sa_setconcurrency(
    long long res, long long concurrency);
void __sanitizer_syscall_pre_impl_compat_60_sa_yield(void);
void __sanitizer_syscall_post_impl_compat_60_sa_yield(long long res);
void __sanitizer_syscall_pre_impl_compat_60_sa_preempt(long long sa_id);
void __sanitizer_syscall_post_impl_compat_60_sa_preempt(long long res,
                                                        long long sa_id);
/* syscall 336 has been skipped */
/* syscall 337 has been skipped */
/* syscall 338 has been skipped */
/* syscall 339 has been skipped */
void __sanitizer_syscall_pre_impl___sigaction_sigtramp(long long signum,
                                                       long long nsa,
                                                       long long osa,
                                                       long long tramp,
                                                       long long vers);
void __sanitizer_syscall_post_impl___sigaction_sigtramp(
    long long res, long long signum, long long nsa, long long osa,
    long long tramp, long long vers);
/* syscall 341 has been skipped */
/* syscall 342 has been skipped */
void __sanitizer_syscall_pre_impl_rasctl(long long addr, long long len,
                                         long long op);
void __sanitizer_syscall_post_impl_rasctl(long long res, long long addr,
                                          long long len, long long op);
void __sanitizer_syscall_pre_impl_kqueue(void);
void __sanitizer_syscall_post_impl_kqueue(long long res);
void __sanitizer_syscall_pre_impl_compat_50_kevent(
    long long fd, long long changelist, long long nchanges, long long eventlist,
    long long nevents, long long timeout);
void __sanitizer_syscall_post_impl_compat_50_kevent(
    long long res, long long fd, long long changelist, long long nchanges,
    long long eventlist, long long nevents, long long timeout);
void __sanitizer_syscall_pre_impl__sched_setparam(long long pid, long long lid,
                                                  long long policy,
                                                  long long params);
void __sanitizer_syscall_post_impl__sched_setparam(long long res, long long pid,
                                                   long long lid,
                                                   long long policy,
                                                   long long params);
void __sanitizer_syscall_pre_impl__sched_getparam(long long pid, long long lid,
                                                  long long policy,
                                                  long long params);
void __sanitizer_syscall_post_impl__sched_getparam(long long res, long long pid,
                                                   long long lid,
                                                   long long policy,
                                                   long long params);
void __sanitizer_syscall_pre_impl__sched_setaffinity(long long pid,
                                                     long long lid,
                                                     long long size,
                                                     long long cpuset);
void __sanitizer_syscall_post_impl__sched_setaffinity(long long res,
                                                      long long pid,
                                                      long long lid,
                                                      long long size,
                                                      long long cpuset);
void __sanitizer_syscall_pre_impl__sched_getaffinity(long long pid,
                                                     long long lid,
                                                     long long size,
                                                     long long cpuset);
void __sanitizer_syscall_post_impl__sched_getaffinity(long long res,
                                                      long long pid,
                                                      long long lid,
                                                      long long size,
                                                      long long cpuset);
void __sanitizer_syscall_pre_impl_sched_yield(void);
void __sanitizer_syscall_post_impl_sched_yield(long long res);
void __sanitizer_syscall_pre_impl__sched_protect(long long priority);
void __sanitizer_syscall_post_impl__sched_protect(long long res,
                                                  long long priority);
/* syscall 352 has been skipped */
/* syscall 353 has been skipped */
void __sanitizer_syscall_pre_impl_fsync_range(long long fd, long long flags,
                                              long long start,
                                              long long length);
void __sanitizer_syscall_post_impl_fsync_range(long long res, long long fd,
                                               long long flags, long long start,
                                               long long length);
void __sanitizer_syscall_pre_impl_uuidgen(long long store, long long count);
void __sanitizer_syscall_post_impl_uuidgen(long long res, long long store,
                                           long long count);
void __sanitizer_syscall_pre_impl_getvfsstat(long long buf, long long bufsize,
                                             long long flags);
void __sanitizer_syscall_post_impl_getvfsstat(long long res, long long buf,
                                              long long bufsize,
                                              long long flags);
void __sanitizer_syscall_pre_impl_statvfs1(long long path, long long buf,
                                           long long flags);
void __sanitizer_syscall_post_impl_statvfs1(long long res, long long path,
                                            long long buf, long long flags);
void __sanitizer_syscall_pre_impl_fstatvfs1(long long fd, long long buf,
                                            long long flags);
void __sanitizer_syscall_post_impl_fstatvfs1(long long res, long long fd,
                                             long long buf, long long flags);
void __sanitizer_syscall_pre_impl_compat_30_fhstatvfs1(long long fhp,
                                                       long long buf,
                                                       long long flags);
void __sanitizer_syscall_post_impl_compat_30_fhstatvfs1(long long res,
                                                        long long fhp,
                                                        long long buf,
                                                        long long flags);
void __sanitizer_syscall_pre_impl_extattrctl(long long path, long long cmd,
                                             long long filename,
                                             long long attrnamespace,
                                             long long attrname);
void __sanitizer_syscall_post_impl_extattrctl(long long res, long long path,
                                              long long cmd, long long filename,
                                              long long attrnamespace,
                                              long long attrname);
void __sanitizer_syscall_pre_impl_extattr_set_file(long long path,
                                                   long long attrnamespace,
                                                   long long attrname,
                                                   long long data,
                                                   long long nbytes);
void __sanitizer_syscall_post_impl_extattr_set_file(
    long long res, long long path, long long attrnamespace, long long attrname,
    long long data, long long nbytes);
void __sanitizer_syscall_pre_impl_extattr_get_file(long long path,
                                                   long long attrnamespace,
                                                   long long attrname,
                                                   long long data,
                                                   long long nbytes);
void __sanitizer_syscall_post_impl_extattr_get_file(
    long long res, long long path, long long attrnamespace, long long attrname,
    long long data, long long nbytes);
void __sanitizer_syscall_pre_impl_extattr_delete_file(long long path,
                                                      long long attrnamespace,
                                                      long long attrname);
void __sanitizer_syscall_post_impl_extattr_delete_file(long long res,
                                                       long long path,
                                                       long long attrnamespace,
                                                       long long attrname);
void __sanitizer_syscall_pre_impl_extattr_set_fd(long long fd,
                                                 long long attrnamespace,
                                                 long long attrname,
                                                 long long data,
                                                 long long nbytes);
void __sanitizer_syscall_post_impl_extattr_set_fd(long long res, long long fd,
                                                  long long attrnamespace,
                                                  long long attrname,
                                                  long long data,
                                                  long long nbytes);
void __sanitizer_syscall_pre_impl_extattr_get_fd(long long fd,
                                                 long long attrnamespace,
                                                 long long attrname,
                                                 long long data,
                                                 long long nbytes);
void __sanitizer_syscall_post_impl_extattr_get_fd(long long res, long long fd,
                                                  long long attrnamespace,
                                                  long long attrname,
                                                  long long data,
                                                  long long nbytes);
void __sanitizer_syscall_pre_impl_extattr_delete_fd(long long fd,
                                                    long long attrnamespace,
                                                    long long attrname);
void __sanitizer_syscall_post_impl_extattr_delete_fd(long long res,
                                                     long long fd,
                                                     long long attrnamespace,
                                                     long long attrname);
void __sanitizer_syscall_pre_impl_extattr_set_link(long long path,
                                                   long long attrnamespace,
                                                   long long attrname,
                                                   long long data,
                                                   long long nbytes);
void __sanitizer_syscall_post_impl_extattr_set_link(
    long long res, long long path, long long attrnamespace, long long attrname,
    long long data, long long nbytes);
void __sanitizer_syscall_pre_impl_extattr_get_link(long long path,
                                                   long long attrnamespace,
                                                   long long attrname,
                                                   long long data,
                                                   long long nbytes);
void __sanitizer_syscall_post_impl_extattr_get_link(
    long long res, long long path, long long attrnamespace, long long attrname,
    long long data, long long nbytes);
void __sanitizer_syscall_pre_impl_extattr_delete_link(long long path,
                                                      long long attrnamespace,
                                                      long long attrname);
void __sanitizer_syscall_post_impl_extattr_delete_link(long long res,
                                                       long long path,
                                                       long long attrnamespace,
                                                       long long attrname);
void __sanitizer_syscall_pre_impl_extattr_list_fd(long long fd,
                                                  long long attrnamespace,
                                                  long long data,
                                                  long long nbytes);
void __sanitizer_syscall_post_impl_extattr_list_fd(long long res, long long fd,
                                                   long long attrnamespace,
                                                   long long data,
                                                   long long nbytes);
void __sanitizer_syscall_pre_impl_extattr_list_file(long long path,
                                                    long long attrnamespace,
                                                    long long data,
                                                    long long nbytes);
void __sanitizer_syscall_post_impl_extattr_list_file(long long res,
                                                     long long path,
                                                     long long attrnamespace,
                                                     long long data,
                                                     long long nbytes);
void __sanitizer_syscall_pre_impl_extattr_list_link(long long path,
                                                    long long attrnamespace,
                                                    long long data,
                                                    long long nbytes);
void __sanitizer_syscall_post_impl_extattr_list_link(long long res,
                                                     long long path,
                                                     long long attrnamespace,
                                                     long long data,
                                                     long long nbytes);
void __sanitizer_syscall_pre_impl_compat_50_pselect(long long nd, long long in,
                                                    long long ou, long long ex,
                                                    long long ts,
                                                    long long mask);
void __sanitizer_syscall_post_impl_compat_50_pselect(long long res,
                                                     long long nd, long long in,
                                                     long long ou, long long ex,
                                                     long long ts,
                                                     long long mask);
void __sanitizer_syscall_pre_impl_compat_50_pollts(long long fds,
                                                   long long nfds, long long ts,
                                                   long long mask);
void __sanitizer_syscall_post_impl_compat_50_pollts(
    long long res, long long fds, long long nfds, long long ts, long long mask);
void __sanitizer_syscall_pre_impl_setxattr(long long path, long long name,
                                           long long value, long long size,
                                           long long flags);
void __sanitizer_syscall_post_impl_setxattr(long long res, long long path,
                                            long long name, long long value,
                                            long long size, long long flags);
void __sanitizer_syscall_pre_impl_lsetxattr(long long path, long long name,
                                            long long value, long long size,
                                            long long flags);
void __sanitizer_syscall_post_impl_lsetxattr(long long res, long long path,
                                             long long name, long long value,
                                             long long size, long long flags);
void __sanitizer_syscall_pre_impl_fsetxattr(long long fd, long long name,
                                            long long value, long long size,
                                            long long flags);
void __sanitizer_syscall_post_impl_fsetxattr(long long res, long long fd,
                                             long long name, long long value,
                                             long long size, long long flags);
void __sanitizer_syscall_pre_impl_getxattr(long long path, long long name,
                                           long long value, long long size);
void __sanitizer_syscall_post_impl_getxattr(long long res, long long path,
                                            long long name, long long value,
                                            long long size);
void __sanitizer_syscall_pre_impl_lgetxattr(long long path, long long name,
                                            long long value, long long size);
void __sanitizer_syscall_post_impl_lgetxattr(long long res, long long path,
                                             long long name, long long value,
                                             long long size);
void __sanitizer_syscall_pre_impl_fgetxattr(long long fd, long long name,
                                            long long value, long long size);
void __sanitizer_syscall_post_impl_fgetxattr(long long res, long long fd,
                                             long long name, long long value,
                                             long long size);
void __sanitizer_syscall_pre_impl_listxattr(long long path, long long list,
                                            long long size);
void __sanitizer_syscall_post_impl_listxattr(long long res, long long path,
                                             long long list, long long size);
void __sanitizer_syscall_pre_impl_llistxattr(long long path, long long list,
                                             long long size);
void __sanitizer_syscall_post_impl_llistxattr(long long res, long long path,
                                              long long list, long long size);
void __sanitizer_syscall_pre_impl_flistxattr(long long fd, long long list,
                                             long long size);
void __sanitizer_syscall_post_impl_flistxattr(long long res, long long fd,
                                              long long list, long long size);
void __sanitizer_syscall_pre_impl_removexattr(long long path, long long name);
void __sanitizer_syscall_post_impl_removexattr(long long res, long long path,
                                               long long name);
void __sanitizer_syscall_pre_impl_lremovexattr(long long path, long long name);
void __sanitizer_syscall_post_impl_lremovexattr(long long res, long long path,
                                                long long name);
void __sanitizer_syscall_pre_impl_fremovexattr(long long fd, long long name);
void __sanitizer_syscall_post_impl_fremovexattr(long long res, long long fd,
                                                long long name);
void __sanitizer_syscall_pre_impl_compat_50___stat30(long long path,
                                                     long long ub);
void __sanitizer_syscall_post_impl_compat_50___stat30(long long res,
                                                      long long path,
                                                      long long ub);
void __sanitizer_syscall_pre_impl_compat_50___fstat30(long long fd,
                                                      long long sb);
void __sanitizer_syscall_post_impl_compat_50___fstat30(long long res,
                                                       long long fd,
                                                       long long sb);
void __sanitizer_syscall_pre_impl_compat_50___lstat30(long long path,
                                                      long long ub);
void __sanitizer_syscall_post_impl_compat_50___lstat30(long long res,
                                                       long long path,
                                                       long long ub);
void __sanitizer_syscall_pre_impl___getdents30(long long fd, long long buf,
                                               long long count);
void __sanitizer_syscall_post_impl___getdents30(long long res, long long fd,
                                                long long buf, long long count);
void __sanitizer_syscall_pre_impl_posix_fadvise(long long);
void __sanitizer_syscall_post_impl_posix_fadvise(long long res, long long);
void __sanitizer_syscall_pre_impl_compat_30___fhstat30(long long fhp,
                                                       long long sb);
void __sanitizer_syscall_post_impl_compat_30___fhstat30(long long res,
                                                        long long fhp,
                                                        long long sb);
void __sanitizer_syscall_pre_impl_compat_50___ntp_gettime30(long long ntvp);
void __sanitizer_syscall_post_impl_compat_50___ntp_gettime30(long long res,
                                                             long long ntvp);
void __sanitizer_syscall_pre_impl___socket30(long long domain, long long type,
                                             long long protocol);
void __sanitizer_syscall_post_impl___socket30(long long res, long long domain,
                                              long long type,
                                              long long protocol);
void __sanitizer_syscall_pre_impl___getfh30(long long fname, long long fhp,
                                            long long fh_size);
void __sanitizer_syscall_post_impl___getfh30(long long res, long long fname,
                                             long long fhp, long long fh_size);
void __sanitizer_syscall_pre_impl___fhopen40(long long fhp, long long fh_size,
                                             long long flags);
void __sanitizer_syscall_post_impl___fhopen40(long long res, long long fhp,
                                              long long fh_size,
                                              long long flags);
void __sanitizer_syscall_pre_impl___fhstatvfs140(long long fhp,
                                                 long long fh_size,
                                                 long long buf,
                                                 long long flags);
void __sanitizer_syscall_post_impl___fhstatvfs140(long long res, long long fhp,
                                                  long long fh_size,
                                                  long long buf,
                                                  long long flags);
void __sanitizer_syscall_pre_impl_compat_50___fhstat40(long long fhp,
                                                       long long fh_size,
                                                       long long sb);
void __sanitizer_syscall_post_impl_compat_50___fhstat40(long long res,
                                                        long long fhp,
                                                        long long fh_size,
                                                        long long sb);
void __sanitizer_syscall_pre_impl_aio_cancel(long long fildes,
                                             long long aiocbp);
void __sanitizer_syscall_post_impl_aio_cancel(long long res, long long fildes,
                                              long long aiocbp);
void __sanitizer_syscall_pre_impl_aio_error(long long aiocbp);
void __sanitizer_syscall_post_impl_aio_error(long long res, long long aiocbp);
void __sanitizer_syscall_pre_impl_aio_fsync(long long op, long long aiocbp);
void __sanitizer_syscall_post_impl_aio_fsync(long long res, long long op,
                                             long long aiocbp);
void __sanitizer_syscall_pre_impl_aio_read(long long aiocbp);
void __sanitizer_syscall_post_impl_aio_read(long long res, long long aiocbp);
void __sanitizer_syscall_pre_impl_aio_return(long long aiocbp);
void __sanitizer_syscall_post_impl_aio_return(long long res, long long aiocbp);
void __sanitizer_syscall_pre_impl_compat_50_aio_suspend(long long list,
                                                        long long nent,
                                                        long long timeout);
void __sanitizer_syscall_post_impl_compat_50_aio_suspend(long long res,
                                                         long long list,
                                                         long long nent,
                                                         long long timeout);
void __sanitizer_syscall_pre_impl_aio_write(long long aiocbp);
void __sanitizer_syscall_post_impl_aio_write(long long res, long long aiocbp);
void __sanitizer_syscall_pre_impl_lio_listio(long long mode, long long list,
                                             long long nent, long long sig);
void __sanitizer_syscall_post_impl_lio_listio(long long res, long long mode,
                                              long long list, long long nent,
                                              long long sig);
/* syscall 407 has been skipped */
/* syscall 408 has been skipped */
/* syscall 409 has been skipped */
void __sanitizer_syscall_pre_impl___mount50(long long type, long long path,
                                            long long flags, long long data,
                                            long long data_len);
void __sanitizer_syscall_post_impl___mount50(long long res, long long type,
                                             long long path, long long flags,
                                             long long data,
                                             long long data_len);
void __sanitizer_syscall_pre_impl_mremap(long long old_address,
                                         long long old_size,
                                         long long new_address,
                                         long long new_size, long long flags);
void __sanitizer_syscall_post_impl_mremap(long long res, long long old_address,
                                          long long old_size,
                                          long long new_address,
                                          long long new_size, long long flags);
void __sanitizer_syscall_pre_impl_pset_create(long long psid);
void __sanitizer_syscall_post_impl_pset_create(long long res, long long psid);
void __sanitizer_syscall_pre_impl_pset_destroy(long long psid);
void __sanitizer_syscall_post_impl_pset_destroy(long long res, long long psid);
void __sanitizer_syscall_pre_impl_pset_assign(long long psid, long long cpuid,
                                              long long opsid);
void __sanitizer_syscall_post_impl_pset_assign(long long res, long long psid,
                                               long long cpuid,
                                               long long opsid);
void __sanitizer_syscall_pre_impl__pset_bind(long long idtype,
                                             long long first_id,
                                             long long second_id,
                                             long long psid, long long opsid);
void __sanitizer_syscall_post_impl__pset_bind(long long res, long long idtype,
                                              long long first_id,
                                              long long second_id,
                                              long long psid, long long opsid);
void __sanitizer_syscall_pre_impl___posix_fadvise50(long long fd, long long PAD,
                                                    long long offset,
                                                    long long len,
                                                    long long advice);
void __sanitizer_syscall_post_impl___posix_fadvise50(
    long long res, long long fd, long long PAD, long long offset, long long len,
    long long advice);
void __sanitizer_syscall_pre_impl___select50(long long nd, long long in,
                                             long long ou, long long ex,
                                             long long tv);
void __sanitizer_syscall_post_impl___select50(long long res, long long nd,
                                              long long in, long long ou,
                                              long long ex, long long tv);
void __sanitizer_syscall_pre_impl___gettimeofday50(long long tp, long long tzp);
void __sanitizer_syscall_post_impl___gettimeofday50(long long res, long long tp,
                                                    long long tzp);
void __sanitizer_syscall_pre_impl___settimeofday50(long long tv, long long tzp);
void __sanitizer_syscall_post_impl___settimeofday50(long long res, long long tv,
                                                    long long tzp);
void __sanitizer_syscall_pre_impl___utimes50(long long path, long long tptr);
void __sanitizer_syscall_post_impl___utimes50(long long res, long long path,
                                              long long tptr);
void __sanitizer_syscall_pre_impl___adjtime50(long long delta,
                                              long long olddelta);
void __sanitizer_syscall_post_impl___adjtime50(long long res, long long delta,
                                               long long olddelta);
void __sanitizer_syscall_pre_impl___lfs_segwait50(long long fsidp,
                                                  long long tv);
void __sanitizer_syscall_post_impl___lfs_segwait50(long long res,
                                                   long long fsidp,
                                                   long long tv);
void __sanitizer_syscall_pre_impl___futimes50(long long fd, long long tptr);
void __sanitizer_syscall_post_impl___futimes50(long long res, long long fd,
                                               long long tptr);
void __sanitizer_syscall_pre_impl___lutimes50(long long path, long long tptr);
void __sanitizer_syscall_post_impl___lutimes50(long long res, long long path,
                                               long long tptr);
void __sanitizer_syscall_pre_impl___setitimer50(long long which, long long itv,
                                                long long oitv);
void __sanitizer_syscall_post_impl___setitimer50(long long res, long long which,
                                                 long long itv, long long oitv);
void __sanitizer_syscall_pre_impl___getitimer50(long long which, long long itv);
void __sanitizer_syscall_post_impl___getitimer50(long long res, long long which,
                                                 long long itv);
void __sanitizer_syscall_pre_impl___clock_gettime50(long long clock_id,
                                                    long long tp);
void __sanitizer_syscall_post_impl___clock_gettime50(long long res,
                                                     long long clock_id,
                                                     long long tp);
void __sanitizer_syscall_pre_impl___clock_settime50(long long clock_id,
                                                    long long tp);
void __sanitizer_syscall_post_impl___clock_settime50(long long res,
                                                     long long clock_id,
                                                     long long tp);
void __sanitizer_syscall_pre_impl___clock_getres50(long long clock_id,
                                                   long long tp);
void __sanitizer_syscall_post_impl___clock_getres50(long long res,
                                                    long long clock_id,
                                                    long long tp);
void __sanitizer_syscall_pre_impl___nanosleep50(long long rqtp, long long rmtp);
void __sanitizer_syscall_post_impl___nanosleep50(long long res, long long rqtp,
                                                 long long rmtp);
void __sanitizer_syscall_pre_impl_____sigtimedwait50(long long set,
                                                     long long info,
                                                     long long timeout);
void __sanitizer_syscall_post_impl_____sigtimedwait50(long long res,
                                                      long long set,
                                                      long long info,
                                                      long long timeout);
void __sanitizer_syscall_pre_impl___mq_timedsend50(long long mqdes,
                                                   long long msg_ptr,
                                                   long long msg_len,
                                                   long long msg_prio,
                                                   long long abs_timeout);
void __sanitizer_syscall_post_impl___mq_timedsend50(
    long long res, long long mqdes, long long msg_ptr, long long msg_len,
    long long msg_prio, long long abs_timeout);
void __sanitizer_syscall_pre_impl___mq_timedreceive50(long long mqdes,
                                                      long long msg_ptr,
                                                      long long msg_len,
                                                      long long msg_prio,
                                                      long long abs_timeout);
void __sanitizer_syscall_post_impl___mq_timedreceive50(
    long long res, long long mqdes, long long msg_ptr, long long msg_len,
    long long msg_prio, long long abs_timeout);
void __sanitizer_syscall_pre_impl_compat_60__lwp_park(long long ts,
                                                      long long unpark,
                                                      long long hint,
                                                      long long unparkhint);
void __sanitizer_syscall_post_impl_compat_60__lwp_park(long long res,
                                                       long long ts,
                                                       long long unpark,
                                                       long long hint,
                                                       long long unparkhint);
void __sanitizer_syscall_pre_impl___kevent50(long long fd, long long changelist,
                                             long long nchanges,
                                             long long eventlist,
                                             long long nevents,
                                             long long timeout);
void __sanitizer_syscall_post_impl___kevent50(
    long long res, long long fd, long long changelist, long long nchanges,
    long long eventlist, long long nevents, long long timeout);
void __sanitizer_syscall_pre_impl___pselect50(long long nd, long long in,
                                              long long ou, long long ex,
                                              long long ts, long long mask);
void __sanitizer_syscall_post_impl___pselect50(long long res, long long nd,
                                               long long in, long long ou,
                                               long long ex, long long ts,
                                               long long mask);
void __sanitizer_syscall_pre_impl___pollts50(long long fds, long long nfds,
                                             long long ts, long long mask);
void __sanitizer_syscall_post_impl___pollts50(long long res, long long fds,
                                              long long nfds, long long ts,
                                              long long mask);
void __sanitizer_syscall_pre_impl___aio_suspend50(long long list,
                                                  long long nent,
                                                  long long timeout);
void __sanitizer_syscall_post_impl___aio_suspend50(long long res,
                                                   long long list,
                                                   long long nent,
                                                   long long timeout);
void __sanitizer_syscall_pre_impl___stat50(long long path, long long ub);
void __sanitizer_syscall_post_impl___stat50(long long res, long long path,
                                            long long ub);
void __sanitizer_syscall_pre_impl___fstat50(long long fd, long long sb);
void __sanitizer_syscall_post_impl___fstat50(long long res, long long fd,
                                             long long sb);
void __sanitizer_syscall_pre_impl___lstat50(long long path, long long ub);
void __sanitizer_syscall_post_impl___lstat50(long long res, long long path,
                                             long long ub);
void __sanitizer_syscall_pre_impl_____semctl50(long long semid,
                                               long long semnum, long long cmd,
                                               long long arg);
void __sanitizer_syscall_post_impl_____semctl50(long long res, long long semid,
                                                long long semnum, long long cmd,
                                                long long arg);
void __sanitizer_syscall_pre_impl___shmctl50(long long shmid, long long cmd,
                                             long long buf);
void __sanitizer_syscall_post_impl___shmctl50(long long res, long long shmid,
                                              long long cmd, long long buf);
void __sanitizer_syscall_pre_impl___msgctl50(long long msqid, long long cmd,
                                             long long buf);
void __sanitizer_syscall_post_impl___msgctl50(long long res, long long msqid,
                                              long long cmd, long long buf);
void __sanitizer_syscall_pre_impl___getrusage50(long long who,
                                                long long rusage);
void __sanitizer_syscall_post_impl___getrusage50(long long res, long long who,
                                                 long long rusage);
void __sanitizer_syscall_pre_impl___timer_settime50(long long timerid,
                                                    long long flags,
                                                    long long value,
                                                    long long ovalue);
void __sanitizer_syscall_post_impl___timer_settime50(long long res,
                                                     long long timerid,
                                                     long long flags,
                                                     long long value,
                                                     long long ovalue);
void __sanitizer_syscall_pre_impl___timer_gettime50(long long timerid,
                                                    long long value);
void __sanitizer_syscall_post_impl___timer_gettime50(long long res,
                                                     long long timerid,
                                                     long long value);
#if defined(NTP) || !defined(_KERNEL_OPT)
void __sanitizer_syscall_pre_impl___ntp_gettime50(long long ntvp);
void __sanitizer_syscall_post_impl___ntp_gettime50(long long res,
                                                   long long ntvp);
#else
/* syscall 448 has been skipped */
#endif
void __sanitizer_syscall_pre_impl___wait450(long long pid, long long status,
                                            long long options,
                                            long long rusage);
void __sanitizer_syscall_post_impl___wait450(long long res, long long pid,
                                             long long status,
                                             long long options,
                                             long long rusage);
void __sanitizer_syscall_pre_impl___mknod50(long long path, long long mode,
                                            long long dev);
void __sanitizer_syscall_post_impl___mknod50(long long res, long long path,
                                             long long mode, long long dev);
void __sanitizer_syscall_pre_impl___fhstat50(long long fhp, long long fh_size,
                                             long long sb);
void __sanitizer_syscall_post_impl___fhstat50(long long res, long long fhp,
                                              long long fh_size, long long sb);
/* syscall 452 has been skipped */
void __sanitizer_syscall_pre_impl_pipe2(long long fildes, long long flags);
void __sanitizer_syscall_post_impl_pipe2(long long res, long long fildes,
                                         long long flags);
void __sanitizer_syscall_pre_impl_dup3(long long from, long long to,
                                       long long flags);
void __sanitizer_syscall_post_impl_dup3(long long res, long long from,
                                        long long to, long long flags);
void __sanitizer_syscall_pre_impl_kqueue1(long long flags);
void __sanitizer_syscall_post_impl_kqueue1(long long res, long long flags);
void __sanitizer_syscall_pre_impl_paccept(long long s, long long name,
                                          long long anamelen, long long mask,
                                          long long flags);
void __sanitizer_syscall_post_impl_paccept(long long res, long long s,
                                           long long name, long long anamelen,
                                           long long mask, long long flags);
void __sanitizer_syscall_pre_impl_linkat(long long fd1, long long name1,
                                         long long fd2, long long name2,
                                         long long flags);
void __sanitizer_syscall_post_impl_linkat(long long res, long long fd1,
                                          long long name1, long long fd2,
                                          long long name2, long long flags);
void __sanitizer_syscall_pre_impl_renameat(long long fromfd, long long from,
                                           long long tofd, long long to);
void __sanitizer_syscall_post_impl_renameat(long long res, long long fromfd,
                                            long long from, long long tofd,
                                            long long to);
void __sanitizer_syscall_pre_impl_mkfifoat(long long fd, long long path,
                                           long long mode);
void __sanitizer_syscall_post_impl_mkfifoat(long long res, long long fd,
                                            long long path, long long mode);
void __sanitizer_syscall_pre_impl_mknodat(long long fd, long long path,
                                          long long mode, long long PAD,
                                          long long dev);
void __sanitizer_syscall_post_impl_mknodat(long long res, long long fd,
                                           long long path, long long mode,
                                           long long PAD, long long dev);
void __sanitizer_syscall_pre_impl_mkdirat(long long fd, long long path,
                                          long long mode);
void __sanitizer_syscall_post_impl_mkdirat(long long res, long long fd,
                                           long long path, long long mode);
void __sanitizer_syscall_pre_impl_faccessat(long long fd, long long path,
                                            long long amode, long long flag);
void __sanitizer_syscall_post_impl_faccessat(long long res, long long fd,
                                             long long path, long long amode,
                                             long long flag);
void __sanitizer_syscall_pre_impl_fchmodat(long long fd, long long path,
                                           long long mode, long long flag);
void __sanitizer_syscall_post_impl_fchmodat(long long res, long long fd,
                                            long long path, long long mode,
                                            long long flag);
void __sanitizer_syscall_pre_impl_fchownat(long long fd, long long path,
                                           long long owner, long long group,
                                           long long flag);
void __sanitizer_syscall_post_impl_fchownat(long long res, long long fd,
                                            long long path, long long owner,
                                            long long group, long long flag);
void __sanitizer_syscall_pre_impl_fexecve(long long fd, long long argp,
                                          long long envp);
void __sanitizer_syscall_post_impl_fexecve(long long res, long long fd,
                                           long long argp, long long envp);
void __sanitizer_syscall_pre_impl_fstatat(long long fd, long long path,
                                          long long buf, long long flag);
void __sanitizer_syscall_post_impl_fstatat(long long res, long long fd,
                                           long long path, long long buf,
                                           long long flag);
void __sanitizer_syscall_pre_impl_utimensat(long long fd, long long path,
                                            long long tptr, long long flag);
void __sanitizer_syscall_post_impl_utimensat(long long res, long long fd,
                                             long long path, long long tptr,
                                             long long flag);
void __sanitizer_syscall_pre_impl_openat(long long fd, long long path,
                                         long long oflags, long long mode);
void __sanitizer_syscall_post_impl_openat(long long res, long long fd,
                                          long long path, long long oflags,
                                          long long mode);
void __sanitizer_syscall_pre_impl_readlinkat(long long fd, long long path,
                                             long long buf, long long bufsize);
void __sanitizer_syscall_post_impl_readlinkat(long long res, long long fd,
                                              long long path, long long buf,
                                              long long bufsize);
void __sanitizer_syscall_pre_impl_symlinkat(long long path1, long long fd,
                                            long long path2);
void __sanitizer_syscall_post_impl_symlinkat(long long res, long long path1,
                                             long long fd, long long path2);
void __sanitizer_syscall_pre_impl_unlinkat(long long fd, long long path,
                                           long long flag);
void __sanitizer_syscall_post_impl_unlinkat(long long res, long long fd,
                                            long long path, long long flag);
void __sanitizer_syscall_pre_impl_futimens(long long fd, long long tptr);
void __sanitizer_syscall_post_impl_futimens(long long res, long long fd,
                                            long long tptr);
void __sanitizer_syscall_pre_impl___quotactl(long long path, long long args);
void __sanitizer_syscall_post_impl___quotactl(long long res, long long path,
                                              long long args);
void __sanitizer_syscall_pre_impl_posix_spawn(long long pid, long long path,
                                              long long file_actions,
                                              long long attrp, long long argv,
                                              long long envp);
void __sanitizer_syscall_post_impl_posix_spawn(long long res, long long pid,
                                               long long path,
                                               long long file_actions,
                                               long long attrp, long long argv,
                                               long long envp);
void __sanitizer_syscall_pre_impl_recvmmsg(long long s, long long mmsg,
                                           long long vlen, long long flags,
                                           long long timeout);
void __sanitizer_syscall_post_impl_recvmmsg(long long res, long long s,
                                            long long mmsg, long long vlen,
                                            long long flags, long long timeout);
void __sanitizer_syscall_pre_impl_sendmmsg(long long s, long long mmsg,
                                           long long vlen, long long flags);
void __sanitizer_syscall_post_impl_sendmmsg(long long res, long long s,
                                            long long mmsg, long long vlen,
                                            long long flags);
void __sanitizer_syscall_pre_impl_clock_nanosleep(long long clock_id,
                                                  long long flags,
                                                  long long rqtp,
                                                  long long rmtp);
void __sanitizer_syscall_post_impl_clock_nanosleep(long long res,
                                                   long long clock_id,
                                                   long long flags,
                                                   long long rqtp,
                                                   long long rmtp);
void __sanitizer_syscall_pre_impl____lwp_park60(long long clock_id,
                                                long long flags, long long ts,
                                                long long unpark,
                                                long long hint,
                                                long long unparkhint);
void __sanitizer_syscall_post_impl____lwp_park60(
    long long res, long long clock_id, long long flags, long long ts,
    long long unpark, long long hint, long long unparkhint);
void __sanitizer_syscall_pre_impl_posix_fallocate(long long fd, long long PAD,
                                                  long long pos, long long len);
void __sanitizer_syscall_post_impl_posix_fallocate(long long res, long long fd,
                                                   long long PAD, long long pos,
                                                   long long len);
void __sanitizer_syscall_pre_impl_fdiscard(long long fd, long long PAD,
                                           long long pos, long long len);
void __sanitizer_syscall_post_impl_fdiscard(long long res, long long fd,
                                            long long PAD, long long pos,
                                            long long len);
void __sanitizer_syscall_pre_impl_wait6(long long idtype, long long id,
                                        long long status, long long options,
                                        long long wru, long long info);
void __sanitizer_syscall_post_impl_wait6(long long res, long long idtype,
                                         long long id, long long status,
                                         long long options, long long wru,
                                         long long info);
void __sanitizer_syscall_pre_impl_clock_getcpuclockid2(long long idtype,
                                                       long long id,
                                                       long long clock_id);
void __sanitizer_syscall_post_impl_clock_getcpuclockid2(long long res,
                                                        long long idtype,
                                                        long long id,
                                                        long long clock_id);

#ifdef __cplusplus
} // extern "C"
#endif

// DO NOT EDIT! THIS FILE HAS BEEN GENERATED!

#endif // SANITIZER_NETBSD_SYSCALL_HOOKS_H
