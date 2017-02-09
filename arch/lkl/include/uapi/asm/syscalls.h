#ifndef _ASM_UAPI_LKL_SYSCALLS_H
#define _ASM_UAPI_LKL_SYSCALLS_H

#include <autoconf.h>
#include <linux/types.h>

typedef __kernel_uid32_t 	qid_t;
typedef __kernel_fd_set		fd_set;
typedef __kernel_mode_t		mode_t;
typedef unsigned short		umode_t;
typedef __u32			nlink_t;
typedef __kernel_off_t		off_t;
typedef __kernel_pid_t		pid_t;
typedef __kernel_key_t		key_t;
typedef __kernel_suseconds_t	suseconds_t;
typedef __kernel_timer_t	timer_t;
typedef __kernel_clockid_t	clockid_t;
typedef __kernel_mqd_t		mqd_t;
typedef __kernel_uid32_t	uid_t;
typedef __kernel_gid32_t	gid_t;
typedef __kernel_uid16_t        uid16_t;
typedef __kernel_gid16_t        gid16_t;
typedef unsigned long		uintptr_t;
#ifdef CONFIG_UID16
typedef __kernel_old_uid_t	old_uid_t;
typedef __kernel_old_gid_t	old_gid_t;
#endif
typedef __kernel_loff_t		loff_t;
typedef __kernel_size_t		size_t;
typedef __kernel_ssize_t	ssize_t;
typedef __kernel_time_t		time_t;
typedef __kernel_clock_t	clock_t;
typedef __u32			u32;
typedef __s32			s32;
typedef __u64			u64;
typedef __s64			s64;

#define __user

#include <asm/unistd.h>
/* Temporary undefine system calls that don't have data types defined in UAPI
 * headers */
#undef __NR_kexec_load
#undef __NR_getcpu
#undef __NR_sched_getattr
#undef __NR_sched_setattr
#undef __NR_sched_setparam
#undef __NR_sched_getparam
#undef __NR_sched_setscheduler
#undef __NR_name_to_handle_at
#undef __NR_open_by_handle_at

#undef __NR_umount
#define __NR_umount __NR_umount2

#ifdef CONFIG_64BIT
#define __NR_newstat __NR3264_stat
#define __NR_newlstat __NR3264_lstat
#define __NR_newfstat __NR3264_fstat
#define __NR_newfstatat __NR3264_fstatat
#endif

#define __NR_mmap_pgoff __NR3264_mmap

#include <linux/time.h>
#include <linux/times.h>
#include <linux/timex.h>
#include <linux/capability.h>
#define __KERNEL__ /* to pull in S_ definitions */
#include <linux/stat.h>
#undef __KERNEL__
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <asm/statfs.h>
#include <asm/stat.h>
#include <linux/bpf.h>
#include <linux/msg.h>
#include <linux/resource.h>
#include <linux/sysinfo.h>
#include <linux/shm.h>
#include <linux/aio_abi.h>
#include <linux/socket.h>
#include <linux/perf_event.h>
#include <linux/sem.h>
#include <linux/futex.h>
#include <linux/poll.h>
#include <linux/mqueue.h>
#include <linux/eventpoll.h>
#include <linux/uio.h>
#include <asm/signal.h>
#include <asm/siginfo.h>
#include <linux/utime.h>
#include <asm/socket.h>

/* Define data structures used in system calls that are not defined in UAPI
 * headers */
struct sockaddr {
	unsigned short int sa_family;
	char sa_data[14];
};

#define __UAPI_DEF_IF_NET_DEVICE_FLAGS_LOWER_UP_DORMANT_ECHO 1
#define __UAPI_DEF_IF_IFNAMSIZ	1
#define __UAPI_DEF_IF_NET_DEVICE_FLAGS 1
#define __UAPI_DEF_IF_IFREQ	1
#define __UAPI_DEF_IF_IFMAP	1
#include <linux/if.h>
#define __UAPI_DEF_IN_IPPROTO	1
#define __UAPI_DEF_IN_ADDR	1
#define __UAPI_DEF_IN6_ADDR	1
#define __UAPI_DEF_IP_MREQ	1
#define __UAPI_DEF_IN_PKTINFO	1
#define __UAPI_DEF_SOCKADDR_IN	1
#define __UAPI_DEF_IN_CLASS	1
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/sockios.h>
#include <linux/route.h>
#include <linux/ipv6_route.h>
#include <linux/ipv6.h>
#include <linux/netlink.h>
#include <linux/neighbour.h>
#include <linux/rtnetlink.h>

#include <linux/kdev_t.h>
#include <asm/irq.h>
#include <linux/virtio_blk.h>
#include <linux/virtio_net.h>
#include <linux/virtio_ring.h>
#include <linux/pkt_sched.h>

struct user_msghdr {
	void		__user *msg_name;	/* ptr to socket address structure */
	int		msg_namelen;		/* size of socket address structure */
	struct iovec	__user *msg_iov;	/* scatter/gather array */
	__kernel_size_t	msg_iovlen;		/* # elements in msg_iov */
	void		__user *msg_control;	/* ancillary data */
	__kernel_size_t	msg_controllen;		/* ancillary data buffer length */
	unsigned int	msg_flags;		/* flags on received message */
};

typedef __u32 key_serial_t;

struct mmsghdr {
	struct user_msghdr  msg_hdr;
	unsigned int        msg_len;
};

struct linux_dirent64 {
	u64		d_ino;
	s64		d_off;
	unsigned short	d_reclen;
	unsigned char	d_type;
	char		d_name[0];
};

struct linux_dirent {
	unsigned long	d_ino;
	unsigned long	d_off;
	unsigned short	d_reclen;
	char		d_name[1];
};

struct ustat {
	__kernel_daddr_t	f_tfree;
	__kernel_ino_t		f_tinode;
	char			f_fname[6];
	char			f_fpack[6];
};

#define AF_UNSPEC       0
#define AF_UNIX         1
#define AF_LOCAL        1
#define AF_INET         2
#define AF_AX25         3
#define AF_IPX          4
#define AF_APPLETALK    5
#define AF_NETROM       6
#define AF_BRIDGE       7
#define AF_ATMPVC       8
#define AF_X25          9
#define AF_INET6        10
#define AF_ROSE         11
#define AF_DECnet       12
#define AF_NETBEUI      13
#define AF_SECURITY     14
#define AF_KEY          15
#define AF_NETLINK      16
#define AF_ROUTE        AF_NETLINK
#define AF_PACKET       17
#define AF_ASH          18
#define AF_ECONET       19
#define AF_ATMSVC       20
#define AF_RDS          21
#define AF_SNA          22
#define AF_IRDA         23
#define AF_PPPOX        24
#define AF_WANPIPE      25
#define AF_LLC          26
#define AF_IB           27
#define AF_MPLS         28
#define AF_CAN          29
#define AF_TIPC         30
#define AF_BLUETOOTH    31
#define AF_IUCV         32
#define AF_RXRPC        33
#define AF_ISDN         34
#define AF_PHONET       35
#define AF_IEEE802154   36
#define AF_CAIF         37
#define AF_ALG          38
#define AF_NFC          39
#define AF_VSOCK        40

#define SOCK_STREAM		1
#define SOCK_DGRAM		2
#define SOCK_RAW		3
#define SOCK_RDM		4
#define SOCK_SEQPACKET		5
#define SOCK_DCCP		6
#define SOCK_PACKET		10

#define MSG_TRUNC 0x20

/* avoid colision with system headers defines */
#define sa_handler sa_handler
#define st_atime st_atime
#define st_mtime st_mtime
#define st_ctime st_ctime
#define s_addr s_addr

long lkl_syscall(long no, long *params);
long lkl_sys_halt(void);

#define __MAP0(m,...)
#define __MAP1(m,t,a) m(t,a)
#define __MAP2(m,t,a,...) m(t,a), __MAP1(m,__VA_ARGS__)
#define __MAP3(m,t,a,...) m(t,a), __MAP2(m,__VA_ARGS__)
#define __MAP4(m,t,a,...) m(t,a), __MAP3(m,__VA_ARGS__)
#define __MAP5(m,t,a,...) m(t,a), __MAP4(m,__VA_ARGS__)
#define __MAP6(m,t,a,...) m(t,a), __MAP5(m,__VA_ARGS__)
#define __MAP(n,...) __MAP##n(__VA_ARGS__)

#define __SC_LONG(t, a) (long)a
#define __SC_TABLE(t, a) {sizeof(t), (long long)(a)}
#define __SC_DECL(t, a) t a

#define LKL_SYSCALL0(name)					       \
	static inline long lkl_sys##name(void)			       \
	{							       \
		long params[6];					       \
		return lkl_syscall(__lkl__NR##name, params);	       \
	}

#if __BITS_PER_LONG == 32
#define LKL_SYSCALLx(x, name, ...)				       \
	static inline						       \
	long lkl_sys##name(__MAP(x, __SC_DECL, __VA_ARGS__))	       \
	{							       \
		struct {					       \
			unsigned int size;				       \
			long long value;			       \
		} lkl_params[x] = { __MAP(x, __SC_TABLE, __VA_ARGS__) }; \
		long params[6], i, k;				       \
		for (i = k = 0;i < x && k < 6;i++, k++) {	       \
			if (lkl_params[i].size > sizeof(long) &&       \
			    k + 1 < 6) {     	   		       \
				params[k] =			       \
					(long)(lkl_params[i].value & (-1UL));	   \
				k++;				       \
				params[k] =			       \
					(long)(lkl_params[i].value >> __BITS_PER_LONG); \
			} else {				       \
				params[k] = (long)(lkl_params[i].value); \
			}					       \
		}						       \
		return lkl_syscall(__lkl__NR##name, params);	       \
	}
#else
#define LKL_SYSCALLx(x, name, ...)				       \
       	static inline						       \
	long lkl_sys##name(__MAP(x, __SC_DECL, __VA_ARGS__))	       \
	{							       \
		long params[6] = { __MAP(x, __SC_LONG, __VA_ARGS__) }; \
		return lkl_syscall(__lkl__NR##name, params);	       \
	}
#endif

#define SYSCALL_DEFINE0(name, ...) LKL_SYSCALL0(name)
#define SYSCALL_DEFINE1(name, ...) LKL_SYSCALLx(1, name, __VA_ARGS__)
#define SYSCALL_DEFINE2(name, ...) LKL_SYSCALLx(2, name, __VA_ARGS__)
#define SYSCALL_DEFINE3(name, ...) LKL_SYSCALLx(3, name, __VA_ARGS__)
#define SYSCALL_DEFINE4(name, ...) LKL_SYSCALLx(4, name, __VA_ARGS__)
#define SYSCALL_DEFINE5(name, ...) LKL_SYSCALLx(5, name, __VA_ARGS__)
#define SYSCALL_DEFINE6(name, ...) LKL_SYSCALLx(6, name, __VA_ARGS__)

#if __BITS_PER_LONG == 32
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#endif

#include <asm/syscall_defs.h>

#if __BITS_PER_LONG == 32
#pragma GCC diagnostic pop
#endif

#endif
