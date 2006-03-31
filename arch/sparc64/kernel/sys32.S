/* $Id: sys32.S,v 1.12 2000/03/24 04:17:37 davem Exp $
 * sys32.S: I-cache tricks for 32-bit compatibility layer simple
 *          conversions.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Jakub Jelinek   (jj@ultra.linux.cz)
 */

#include <linux/config.h>
#include <asm/errno.h>

/* NOTE: call as jump breaks return stack, we have to avoid that */

	.text

#define SIGN1(STUB,SYSCALL,REG1) \
	.align	32; \
	.globl	STUB; \
STUB:	sethi	%hi(SYSCALL), %g1; \
	jmpl	%g1 + %lo(SYSCALL), %g0; \
	sra	REG1, 0, REG1

#define SIGN2(STUB,SYSCALL,REG1,REG2) \
	.align	32; \
	.globl	STUB; \
STUB:	sethi	%hi(SYSCALL), %g1; \
	sra	REG1, 0, REG1; \
	jmpl	%g1 + %lo(SYSCALL), %g0; \
	sra	REG2, 0, REG2

#define SIGN3(STUB,SYSCALL,REG1,REG2,REG3) \
	.align	32; \
	.globl	STUB; \
STUB:	sra	REG1, 0, REG1; \
	sethi	%hi(SYSCALL), %g1; \
	sra	REG2, 0, REG2; \
	jmpl	%g1 + %lo(SYSCALL), %g0; \
	sra	REG3, 0, REG3

#define SIGN4(STUB,SYSCALL,REG1,REG2,REG3,REG4) \
	.align	32; \
	.globl	STUB; \
STUB:	sra	REG1, 0, REG1; \
	sethi	%hi(SYSCALL), %g1; \
	sra	REG2, 0, REG2; \
	sra	REG3, 0, REG3; \
	jmpl	%g1 + %lo(SYSCALL), %g0; \
	sra	REG4, 0, REG4

SIGN1(sys32_exit, sparc_exit, %o0)
SIGN1(sys32_exit_group, sys_exit_group, %o0)
SIGN1(sys32_wait4, compat_sys_wait4, %o2)
SIGN1(sys32_creat, sys_creat, %o1)
SIGN1(sys32_mknod, sys_mknod, %o1)
SIGN1(sys32_perfctr, sys_perfctr, %o0)
SIGN1(sys32_umount, sys_umount, %o1)
SIGN1(sys32_signal, sys_signal, %o0)
SIGN1(sys32_access, sys_access, %o1)
SIGN1(sys32_msync, sys_msync, %o2)
SIGN2(sys32_reboot, sys_reboot, %o0, %o1)
SIGN1(sys32_setitimer, compat_sys_setitimer, %o0)
SIGN1(sys32_getitimer, compat_sys_getitimer, %o0)
SIGN1(sys32_sethostname, sys_sethostname, %o1)
SIGN1(sys32_swapon, sys_swapon, %o1)
SIGN1(sys32_sigaction, compat_sys_sigaction, %o0)
SIGN1(sys32_rt_sigaction, compat_sys_rt_sigaction, %o0)
SIGN1(sys32_sigprocmask, compat_sys_sigprocmask, %o0)
SIGN1(sys32_rt_sigprocmask, compat_sys_rt_sigprocmask, %o0)
SIGN2(sys32_rt_sigqueueinfo, compat_sys_rt_sigqueueinfo, %o0, %o1)
SIGN1(sys32_getrusage, compat_sys_getrusage, %o0)
SIGN1(sys32_setxattr, sys_setxattr, %o4)
SIGN1(sys32_lsetxattr, sys_lsetxattr, %o4)
SIGN1(sys32_fsetxattr, sys_fsetxattr, %o4)
SIGN1(sys32_fgetxattr, sys_fgetxattr, %o0)
SIGN1(sys32_flistxattr, sys_flistxattr, %o0)
SIGN1(sys32_fremovexattr, sys_fremovexattr, %o0)
SIGN2(sys32_tkill, sys_tkill, %o0, %o1)
SIGN1(sys32_epoll_create, sys_epoll_create, %o0)
SIGN3(sys32_epoll_ctl, sys_epoll_ctl, %o0, %o1, %o2)
SIGN3(sys32_epoll_wait, sys_epoll_wait, %o0, %o2, %o3)
SIGN1(sys32_readahead, compat_sys_readahead, %o0)
SIGN2(sys32_fadvise64, compat_sys_fadvise64, %o0, %o4)
SIGN2(sys32_fadvise64_64, compat_sys_fadvise64_64, %o0, %o5)
SIGN2(sys32_bdflush, sys_bdflush, %o0, %o1)
SIGN1(sys32_mlockall, sys_mlockall, %o0)
SIGN1(sys32_nfsservctl, compat_sys_nfsservctl, %o0)
SIGN1(sys32_clock_nanosleep, compat_sys_clock_nanosleep, %o1)
SIGN1(sys32_timer_settime, compat_sys_timer_settime, %o1)
SIGN1(sys32_io_submit, compat_sys_io_submit, %o1)
SIGN1(sys32_mq_open, compat_sys_mq_open, %o1)
SIGN1(sys32_select, compat_sys_select, %o0)
SIGN1(sys32_mkdir, sys_mkdir, %o1)
SIGN3(sys32_futex, compat_sys_futex, %o1, %o2, %o5)
SIGN1(sys32_sysfs, compat_sys_sysfs, %o0)
SIGN3(sys32_ipc, compat_sys_ipc, %o1, %o2, %o3)
SIGN2(sys32_sendfile, compat_sys_sendfile, %o0, %o1)
SIGN2(sys32_sendfile64, compat_sys_sendfile64, %o0, %o1)
SIGN1(sys32_prctl, sys_prctl, %o0)
SIGN1(sys32_sched_rr_get_interval, compat_sys_sched_rr_get_interval, %o0)
SIGN2(sys32_waitpid, sys_waitpid, %o0, %o2)
SIGN1(sys32_getgroups, sys_getgroups, %o0)
SIGN1(sys32_getpgid, sys_getpgid, %o0)
SIGN2(sys32_getpriority, sys_getpriority, %o0, %o1)
SIGN1(sys32_getsid, sys_getsid, %o0)
SIGN2(sys32_kill, sys_kill, %o0, %o1)
SIGN1(sys32_nice, sys_nice, %o0)
SIGN1(sys32_lseek, sys_lseek, %o1)
SIGN2(sys32_open, sparc32_open, %o1, %o2)
SIGN1(sys32_readlink, sys_readlink, %o2)
SIGN1(sys32_sched_get_priority_max, sys_sched_get_priority_max, %o0)
SIGN1(sys32_sched_get_priority_min, sys_sched_get_priority_min, %o0)
SIGN1(sys32_sched_getparam, sys_sched_getparam, %o0)
SIGN1(sys32_sched_getscheduler, sys_sched_getscheduler, %o0)
SIGN1(sys32_sched_setparam, sys_sched_setparam, %o0)
SIGN2(sys32_sched_setscheduler, sys_sched_setscheduler, %o0, %o1)
SIGN1(sys32_getdomainname, sys_getdomainname, %o1)
SIGN1(sys32_setdomainname, sys_setdomainname, %o1)
SIGN1(sys32_setgroups, sys_setgroups, %o0)
SIGN2(sys32_setpgid, sys_setpgid, %o0, %o1)
SIGN3(sys32_setpriority, sys_setpriority, %o0, %o1, %o2)
SIGN1(sys32_ssetmask, sys_ssetmask, %o0)
SIGN2(sys32_syslog, sys_syslog, %o0, %o2)
SIGN1(sys32_umask, sys_umask, %o0)
SIGN3(sys32_tgkill, sys_tgkill, %o0, %o1, %o2)
SIGN1(sys32_sendto, sys_sendto, %o0)
SIGN1(sys32_recvfrom, sys_recvfrom, %o0)
SIGN3(sys32_socket, sys_socket, %o0, %o1, %o2)
SIGN2(sys32_connect, sys_connect, %o0, %o2)
SIGN2(sys32_bind, sys_bind, %o0, %o2)
SIGN2(sys32_listen, sys_listen, %o0, %o1)
SIGN1(sys32_recvmsg, compat_sys_recvmsg, %o0)
SIGN1(sys32_sendmsg, compat_sys_sendmsg, %o0)
SIGN2(sys32_shutdown, sys_shutdown, %o0, %o1)
SIGN3(sys32_socketpair, sys_socketpair, %o0, %o1, %o2)
SIGN1(sys32_getpeername, sys_getpeername, %o0)
SIGN1(sys32_getsockname, sys_getsockname, %o0)
SIGN2(sys32_ioprio_get, sys_ioprio_get, %o0, %o1)
SIGN3(sys32_ioprio_set, sys_ioprio_set, %o0, %o1, %o2)
SIGN2(sys32_splice, sys_splice, %o0, %o1)

	.globl		sys32_mmap2
sys32_mmap2:
	sethi		%hi(sys_mmap), %g1
	jmpl		%g1 + %lo(sys_mmap), %g0
	 sllx		%o5, 12, %o5

	.align		32
	.globl		sys32_socketcall
sys32_socketcall:	/* %o0=call, %o1=args */
	cmp		%o0, 1
	bl,pn		%xcc, do_einval
	 cmp		%o0, 17
	bg,pn		%xcc, do_einval
	 sub		%o0, 1, %o0
	sllx		%o0, 5, %o0
	sethi		%hi(__socketcall_table_begin), %g2
	or		%g2, %lo(__socketcall_table_begin), %g2
	jmpl		%g2 + %o0, %g0
	 nop
do_einval:
	retl
	 mov		-EINVAL, %o0

	.align		32
__socketcall_table_begin:

	/* Each entry is exactly 32 bytes. */
do_sys_socket: /* sys_socket(int, int, int) */
1:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(sys_socket), %g1
2:	ldswa		[%o1 + 0x8] %asi, %o2
	jmpl		%g1 + %lo(sys_socket), %g0
3:	 ldswa		[%o1 + 0x4] %asi, %o1
	nop
	nop
	nop
do_sys_bind: /* sys_bind(int fd, struct sockaddr *, int) */
4:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(sys_bind), %g1
5:	ldswa		[%o1 + 0x8] %asi, %o2
	jmpl		%g1 + %lo(sys_bind), %g0
6:	 lduwa		[%o1 + 0x4] %asi, %o1
	nop
	nop
	nop
do_sys_connect: /* sys_connect(int, struct sockaddr *, int) */
7:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(sys_connect), %g1
8:	ldswa		[%o1 + 0x8] %asi, %o2
	jmpl		%g1 + %lo(sys_connect), %g0
9:	 lduwa		[%o1 + 0x4] %asi, %o1
	nop
	nop
	nop
do_sys_listen: /* sys_listen(int, int) */
10:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(sys_listen), %g1
	jmpl		%g1 + %lo(sys_listen), %g0
11:	 ldswa		[%o1 + 0x4] %asi, %o1
	nop
	nop
	nop
	nop
do_sys_accept: /* sys_accept(int, struct sockaddr *, int *) */
12:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(sys_accept), %g1
13:	lduwa		[%o1 + 0x8] %asi, %o2
	jmpl		%g1 + %lo(sys_accept), %g0
14:	 lduwa		[%o1 + 0x4] %asi, %o1
	nop
	nop
	nop
do_sys_getsockname: /* sys_getsockname(int, struct sockaddr *, int *) */
15:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(sys_getsockname), %g1
16:	lduwa		[%o1 + 0x8] %asi, %o2
	jmpl		%g1 + %lo(sys_getsockname), %g0
17:	 lduwa		[%o1 + 0x4] %asi, %o1
	nop
	nop
	nop
do_sys_getpeername: /* sys_getpeername(int, struct sockaddr *, int *) */
18:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(sys_getpeername), %g1
19:	lduwa		[%o1 + 0x8] %asi, %o2
	jmpl		%g1 + %lo(sys_getpeername), %g0
20:	 lduwa		[%o1 + 0x4] %asi, %o1
	nop
	nop
	nop
do_sys_socketpair: /* sys_socketpair(int, int, int, int *) */
21:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(sys_socketpair), %g1
22:	ldswa		[%o1 + 0x8] %asi, %o2
23:	lduwa		[%o1 + 0xc] %asi, %o3
	jmpl		%g1 + %lo(sys_socketpair), %g0
24:	 ldswa		[%o1 + 0x4] %asi, %o1
	nop
	nop
do_sys_send: /* sys_send(int, void *, size_t, unsigned int) */
25:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(sys_send), %g1
26:	lduwa		[%o1 + 0x8] %asi, %o2
27:	lduwa		[%o1 + 0xc] %asi, %o3
	jmpl		%g1 + %lo(sys_send), %g0
28:	 lduwa		[%o1 + 0x4] %asi, %o1
	nop
	nop
do_sys_recv: /* sys_recv(int, void *, size_t, unsigned int) */
29:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(sys_recv), %g1
30:	lduwa		[%o1 + 0x8] %asi, %o2
31:	lduwa		[%o1 + 0xc] %asi, %o3
	jmpl		%g1 + %lo(sys_recv), %g0
32:	 lduwa		[%o1 + 0x4] %asi, %o1
	nop
	nop
do_sys_sendto: /* sys_sendto(int, u32, compat_size_t, unsigned int, u32, int) */
33:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(sys_sendto), %g1
34:	lduwa		[%o1 + 0x8] %asi, %o2
35:	lduwa		[%o1 + 0xc] %asi, %o3
36:	lduwa		[%o1 + 0x10] %asi, %o4
37:	ldswa		[%o1 + 0x14] %asi, %o5
	jmpl		%g1 + %lo(sys_sendto), %g0
38:	 lduwa		[%o1 + 0x4] %asi, %o1
do_sys_recvfrom: /* sys_recvfrom(int, u32, compat_size_t, unsigned int, u32, u32) */
39:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(sys_recvfrom), %g1
40:	lduwa		[%o1 + 0x8] %asi, %o2
41:	lduwa		[%o1 + 0xc] %asi, %o3
42:	lduwa		[%o1 + 0x10] %asi, %o4
43:	lduwa		[%o1 + 0x14] %asi, %o5
	jmpl		%g1 + %lo(sys_recvfrom), %g0
44:	 lduwa		[%o1 + 0x4] %asi, %o1
do_sys_shutdown: /* sys_shutdown(int, int) */
45:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(sys_shutdown), %g1
	jmpl		%g1 + %lo(sys_shutdown), %g0
46:	 ldswa		[%o1 + 0x4] %asi, %o1
	nop
	nop
	nop
	nop
do_sys_setsockopt: /* compat_sys_setsockopt(int, int, int, char *, int) */
47:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(compat_sys_setsockopt), %g1
48:	ldswa		[%o1 + 0x8] %asi, %o2
49:	lduwa		[%o1 + 0xc] %asi, %o3
50:	ldswa		[%o1 + 0x10] %asi, %o4
	jmpl		%g1 + %lo(compat_sys_setsockopt), %g0
51:	 ldswa		[%o1 + 0x4] %asi, %o1
	nop
do_sys_getsockopt: /* compat_sys_getsockopt(int, int, int, u32, u32) */
52:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(compat_sys_getsockopt), %g1
53:	ldswa		[%o1 + 0x8] %asi, %o2
54:	lduwa		[%o1 + 0xc] %asi, %o3
55:	lduwa		[%o1 + 0x10] %asi, %o4
	jmpl		%g1 + %lo(compat_sys_getsockopt), %g0
56:	 ldswa		[%o1 + 0x4] %asi, %o1
	nop
do_sys_sendmsg: /* compat_sys_sendmsg(int, struct compat_msghdr *, unsigned int) */
57:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(compat_sys_sendmsg), %g1
58:	lduwa		[%o1 + 0x8] %asi, %o2
	jmpl		%g1 + %lo(compat_sys_sendmsg), %g0
59:	 lduwa		[%o1 + 0x4] %asi, %o1
	nop
	nop
	nop
do_sys_recvmsg: /* compat_sys_recvmsg(int, struct compat_msghdr *, unsigned int) */
60:	ldswa		[%o1 + 0x0] %asi, %o0
	sethi		%hi(compat_sys_recvmsg), %g1
61:	lduwa		[%o1 + 0x8] %asi, %o2
	jmpl		%g1 + %lo(compat_sys_recvmsg), %g0
62:	 lduwa		[%o1 + 0x4] %asi, %o1
	nop
	nop
	nop

	.section	__ex_table,"a"
	.align		4
	.word		1b, __retl_efault, 2b, __retl_efault
	.word		3b, __retl_efault, 4b, __retl_efault
	.word		5b, __retl_efault, 6b, __retl_efault
	.word		7b, __retl_efault, 8b, __retl_efault
	.word		9b, __retl_efault, 10b, __retl_efault
	.word		11b, __retl_efault, 12b, __retl_efault
	.word		13b, __retl_efault, 14b, __retl_efault
	.word		15b, __retl_efault, 16b, __retl_efault
	.word		17b, __retl_efault, 18b, __retl_efault
	.word		19b, __retl_efault, 20b, __retl_efault
	.word		21b, __retl_efault, 22b, __retl_efault
	.word		23b, __retl_efault, 24b, __retl_efault
	.word		25b, __retl_efault, 26b, __retl_efault
	.word		27b, __retl_efault, 28b, __retl_efault
	.word		29b, __retl_efault, 30b, __retl_efault
	.word		31b, __retl_efault, 32b, __retl_efault
	.word		33b, __retl_efault, 34b, __retl_efault
	.word		35b, __retl_efault, 36b, __retl_efault
	.word		37b, __retl_efault, 38b, __retl_efault
	.word		39b, __retl_efault, 40b, __retl_efault
	.word		41b, __retl_efault, 42b, __retl_efault
	.word		43b, __retl_efault, 44b, __retl_efault
	.word		45b, __retl_efault, 46b, __retl_efault
	.word		47b, __retl_efault, 48b, __retl_efault
	.word		49b, __retl_efault, 50b, __retl_efault
	.word		51b, __retl_efault, 52b, __retl_efault
	.word		53b, __retl_efault, 54b, __retl_efault
	.word		55b, __retl_efault, 56b, __retl_efault
	.word		57b, __retl_efault, 58b, __retl_efault
	.word		59b, __retl_efault, 60b, __retl_efault
	.word		61b, __retl_efault, 62b, __retl_efault
	.previous
