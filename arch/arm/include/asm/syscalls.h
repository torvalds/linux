/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_SYSCALLS_H
#define __ASM_SYSCALLS_H

#include <linux/linkage.h>
#include <linux/types.h>

struct pt_regs;
asmlinkage int sys_sigreturn(struct pt_regs *regs);
asmlinkage int sys_rt_sigreturn(struct pt_regs *regs);
asmlinkage long sys_arm_fadvise64_64(int fd, int advice,
				     loff_t offset, loff_t len);

struct oldabi_stat64;
asmlinkage long sys_oabi_stat64(const char __user * filename,
				struct oldabi_stat64 __user * statbuf);
asmlinkage long sys_oabi_lstat64(const char __user * filename,
				 struct oldabi_stat64 __user * statbuf);
asmlinkage long sys_oabi_fstat64(unsigned long fd,
				 struct oldabi_stat64 __user * statbuf);
asmlinkage long sys_oabi_fstatat64(int dfd,
				   const char __user *filename,
				   struct oldabi_stat64  __user *statbuf,
				   int flag);
asmlinkage long sys_oabi_fcntl64(unsigned int fd, unsigned int cmd,
				 unsigned long arg);
struct oabi_epoll_event;
asmlinkage long sys_oabi_epoll_ctl(int epfd, int op, int fd,
				   struct oabi_epoll_event __user *event);
struct oabi_sembuf;
struct old_timespec32;
asmlinkage long sys_oabi_semtimedop(int semid,
				    struct oabi_sembuf __user *tsops,
				    unsigned nsops,
				    const struct old_timespec32 __user *timeout);
asmlinkage long sys_oabi_semop(int semid, struct oabi_sembuf __user *tsops,
			       unsigned nsops);
asmlinkage int sys_oabi_ipc(uint call, int first, int second, int third,
			    void __user *ptr, long fifth);
struct sockaddr;
asmlinkage long sys_oabi_bind(int fd, struct sockaddr __user *addr, int addrlen);
asmlinkage long sys_oabi_connect(int fd, struct sockaddr __user *addr, int addrlen);
asmlinkage long sys_oabi_sendto(int fd, void __user *buff,
				size_t len, unsigned flags,
				struct sockaddr __user *addr,
				int addrlen);
struct user_msghdr;
asmlinkage long sys_oabi_sendmsg(int fd, struct user_msghdr __user *msg, unsigned flags);
asmlinkage long sys_oabi_socketcall(int call, unsigned long __user *args);

#endif
