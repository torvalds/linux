/* $Id: socket.c,v 1.6 2002/02/08 03:57:14 davem Exp $
 * socket.c: Socket syscall emulation for Solaris 2.6+
 *
 * Copyright (C) 1998 Jakub Jelinek (jj@ultra.linux.cz)
 *
 * 1999-08-19 Fixed socketpair code 
 *            Jason Rappleye (rappleye@ccr.buffalo.edu)
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/file.h>
#include <linux/net.h>
#include <linux/compat.h>
#include <net/compat.h>
#include <net/sock.h>

#include <asm/uaccess.h>
#include <asm/string.h>
#include <asm/oplib.h>
#include <asm/idprom.h>

#include "conv.h"

#define SOCK_SOL_STREAM		2
#define SOCK_SOL_DGRAM		1
#define SOCK_SOL_RAW		4
#define SOCK_SOL_RDM		5
#define SOCK_SOL_SEQPACKET	6

#define SOL_SO_SNDLOWAT		0x1003
#define SOL_SO_RCVLOWAT		0x1004
#define SOL_SO_SNDTIMEO		0x1005
#define SOL_SO_RCVTIMEO		0x1006
#define SOL_SO_STATE		0x2000

#define SOL_SS_NDELAY		0x040
#define SOL_SS_NONBLOCK		0x080
#define SOL_SS_ASYNC		0x100

#define SO_STATE		0x000e

static int socket_check(int family, int type)
{
	if (family != PF_UNIX && family != PF_INET)
		return -ESOCKTNOSUPPORT;
	switch (type) {
	case SOCK_SOL_STREAM: type = SOCK_STREAM; break;
	case SOCK_SOL_DGRAM: type = SOCK_DGRAM; break;
	case SOCK_SOL_RAW: type = SOCK_RAW; break;
	case SOCK_SOL_RDM: type = SOCK_RDM; break;
	case SOCK_SOL_SEQPACKET: type = SOCK_SEQPACKET; break;
	default: return -EINVAL;
	}
	return type;
}

static int solaris_to_linux_sockopt(int optname) 
{
	switch (optname) {
	case SOL_SO_SNDLOWAT: optname = SO_SNDLOWAT; break;
	case SOL_SO_RCVLOWAT: optname = SO_RCVLOWAT; break;
	case SOL_SO_SNDTIMEO: optname = SO_SNDTIMEO; break;
	case SOL_SO_RCVTIMEO: optname = SO_RCVTIMEO; break;
	case SOL_SO_STATE: optname = SO_STATE; break;
	};
	
	return optname;
}
	
asmlinkage int solaris_socket(int family, int type, int protocol)
{
	int (*sys_socket)(int, int, int) =
		(int (*)(int, int, int))SYS(socket);

	type = socket_check (family, type);
	if (type < 0) return type;
	return sys_socket(family, type, protocol);
}

asmlinkage int solaris_socketpair(int *usockvec)
{
	int (*sys_socketpair)(int, int, int, int *) =
		(int (*)(int, int, int, int *))SYS(socketpair);

	/* solaris socketpair really only takes one arg at the syscall
	 * level, int * usockvec. The libs apparently take care of 
	 * making sure that family==AF_UNIX and type==SOCK_STREAM. The 
	 * pointer we really want ends up residing in the first (and
	 * supposedly only) argument.
	 */

	return sys_socketpair(AF_UNIX, SOCK_STREAM, 0, (int *)usockvec);
}

asmlinkage int solaris_bind(int fd, struct sockaddr *addr, int addrlen)
{
	int (*sys_bind)(int, struct sockaddr *, int) =
		(int (*)(int, struct sockaddr *, int))SUNOS(104);

	return sys_bind(fd, addr, addrlen);
}

asmlinkage int solaris_setsockopt(int fd, int level, int optname, u32 optval, int optlen)
{
	int (*sunos_setsockopt)(int, int, int, u32, int) =
		(int (*)(int, int, int, u32, int))SUNOS(105);

	optname = solaris_to_linux_sockopt(optname);
	if (optname < 0)
		return optname;
	if (optname == SO_STATE)
		return 0;

	return sunos_setsockopt(fd, level, optname, optval, optlen);
}

asmlinkage int solaris_getsockopt(int fd, int level, int optname, u32 optval, u32 optlen)
{
	int (*sunos_getsockopt)(int, int, int, u32, u32) =
		(int (*)(int, int, int, u32, u32))SUNOS(118);

	optname = solaris_to_linux_sockopt(optname);
	if (optname < 0)
		return optname;

	if (optname == SO_STATE)
		optname = SOL_SO_STATE;

	return sunos_getsockopt(fd, level, optname, optval, optlen);
}

asmlinkage int solaris_connect(int fd, struct sockaddr __user *addr, int addrlen)
{
	int (*sys_connect)(int, struct sockaddr __user *, int) =
		(int (*)(int, struct sockaddr __user *, int))SYS(connect);

	return sys_connect(fd, addr, addrlen);
}

asmlinkage int solaris_accept(int fd, struct sockaddr __user *addr, int __user *addrlen)
{
	int (*sys_accept)(int, struct sockaddr __user *, int __user *) =
		(int (*)(int, struct sockaddr __user *, int __user *))SYS(accept);

	return sys_accept(fd, addr, addrlen);
}

asmlinkage int solaris_listen(int fd, int backlog)
{
	int (*sys_listen)(int, int) =
		(int (*)(int, int))SUNOS(106);

	return sys_listen(fd, backlog);
}

asmlinkage int solaris_shutdown(int fd, int how)
{
	int (*sys_shutdown)(int, int) =
		(int (*)(int, int))SYS(shutdown);

	return sys_shutdown(fd, how);
}

#define MSG_SOL_OOB		0x1
#define MSG_SOL_PEEK		0x2
#define MSG_SOL_DONTROUTE	0x4
#define MSG_SOL_EOR		0x8
#define MSG_SOL_CTRUNC		0x10
#define MSG_SOL_TRUNC		0x20
#define MSG_SOL_WAITALL		0x40
#define MSG_SOL_DONTWAIT	0x80

static int solaris_to_linux_msgflags(int flags)
{
	int fl = flags & (MSG_OOB|MSG_PEEK|MSG_DONTROUTE);
	
	if (flags & MSG_SOL_EOR) fl |= MSG_EOR;
	if (flags & MSG_SOL_CTRUNC) fl |= MSG_CTRUNC;
	if (flags & MSG_SOL_TRUNC) fl |= MSG_TRUNC;
	if (flags & MSG_SOL_WAITALL) fl |= MSG_WAITALL;
	if (flags & MSG_SOL_DONTWAIT) fl |= MSG_DONTWAIT;
	return fl;
}

static int linux_to_solaris_msgflags(int flags)
{
	int fl = flags & (MSG_OOB|MSG_PEEK|MSG_DONTROUTE);
	
	if (flags & MSG_EOR) fl |= MSG_SOL_EOR;
	if (flags & MSG_CTRUNC) fl |= MSG_SOL_CTRUNC;
	if (flags & MSG_TRUNC) fl |= MSG_SOL_TRUNC;
	if (flags & MSG_WAITALL) fl |= MSG_SOL_WAITALL;
	if (flags & MSG_DONTWAIT) fl |= MSG_SOL_DONTWAIT;
	return fl;
}

asmlinkage int solaris_recvfrom(int s, char __user *buf, int len, int flags, u32 from, u32 fromlen)
{
	int (*sys_recvfrom)(int, void __user *, size_t, unsigned, struct sockaddr __user *, int __user *) =
		(int (*)(int, void __user *, size_t, unsigned, struct sockaddr __user *, int __user *))SYS(recvfrom);
	
	return sys_recvfrom(s, buf, len, solaris_to_linux_msgflags(flags), A(from), A(fromlen));
}

asmlinkage int solaris_recv(int s, char __user *buf, int len, int flags)
{
	int (*sys_recvfrom)(int, void __user *, size_t, unsigned, struct sockaddr __user *, int __user *) =
		(int (*)(int, void __user *, size_t, unsigned, struct sockaddr __user *, int __user *))SYS(recvfrom);
	
	return sys_recvfrom(s, buf, len, solaris_to_linux_msgflags(flags), NULL, NULL);
}

asmlinkage int solaris_sendto(int s, char __user *buf, int len, int flags, u32 to, u32 tolen)
{
	int (*sys_sendto)(int, void __user *, size_t, unsigned, struct sockaddr __user *, int __user *) =
		(int (*)(int, void __user *, size_t, unsigned, struct sockaddr __user *, int __user *))SYS(sendto);
	
	return sys_sendto(s, buf, len, solaris_to_linux_msgflags(flags), A(to), A(tolen));
}

asmlinkage int solaris_send(int s, char *buf, int len, int flags)
{
	int (*sys_sendto)(int, void *, size_t, unsigned, struct sockaddr *, int *) =
		(int (*)(int, void *, size_t, unsigned, struct sockaddr *, int *))SYS(sendto);
	
	return sys_sendto(s, buf, len, solaris_to_linux_msgflags(flags), NULL, NULL);
}

asmlinkage int solaris_getpeername(int fd, struct sockaddr *addr, int *addrlen)
{
	int (*sys_getpeername)(int, struct sockaddr *, int *) =
		(int (*)(int, struct sockaddr *, int *))SYS(getpeername);

	return sys_getpeername(fd, addr, addrlen);
}

asmlinkage int solaris_getsockname(int fd, struct sockaddr *addr, int *addrlen)
{
	int (*sys_getsockname)(int, struct sockaddr *, int *) =
		(int (*)(int, struct sockaddr *, int *))SYS(getsockname);

	return sys_getsockname(fd, addr, addrlen);
}

/* XXX This really belongs in some header file... -DaveM */
#define MAX_SOCK_ADDR	128		/* 108 for Unix domain - 
					   16 for IP, 16 for IPX,
					   24 for IPv6,
					   about 80 for AX.25 */

struct sol_nmsghdr {
	u32		msg_name;
	int		msg_namelen;
	u32		msg_iov;
	u32		msg_iovlen;
	u32		msg_control;
	u32		msg_controllen;
	u32		msg_flags;
};

struct sol_cmsghdr {
	u32		cmsg_len;
	int		cmsg_level;
	int		cmsg_type;
	unsigned char	cmsg_data[0];
};

static inline int msghdr_from_user32_to_kern(struct msghdr *kmsg,
					     struct sol_nmsghdr __user *umsg)
{
	u32 tmp1, tmp2, tmp3;
	int err;

	err = get_user(tmp1, &umsg->msg_name);
	err |= __get_user(tmp2, &umsg->msg_iov);
	err |= __get_user(tmp3, &umsg->msg_control);
	if (err)
		return -EFAULT;

	kmsg->msg_name = A(tmp1);
	kmsg->msg_iov = A(tmp2);
	kmsg->msg_control = A(tmp3);

	err = get_user(kmsg->msg_namelen, &umsg->msg_namelen);
	err |= get_user(kmsg->msg_controllen, &umsg->msg_controllen);
	err |= get_user(kmsg->msg_flags, &umsg->msg_flags);
	
	kmsg->msg_flags = solaris_to_linux_msgflags(kmsg->msg_flags);
	
	return err;
}

asmlinkage int solaris_sendmsg(int fd, struct sol_nmsghdr __user *user_msg, unsigned user_flags)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	struct iovec iovstack[UIO_FASTIOV], *iov = iovstack;
	unsigned char ctl[sizeof(struct cmsghdr) + 20];
	unsigned char *ctl_buf = ctl;
	struct msghdr msg_sys;
	int err, ctl_len, iov_size, total_len;

	err = -EFAULT;
	if (msghdr_from_user32_to_kern(&msg_sys, user_msg))
		goto out;

	sock = sockfd_lookup(fd, &err);
	if (!sock)
		goto out;

	/* do not move before msg_sys is valid */
	err = -EMSGSIZE;
	if (msg_sys.msg_iovlen > UIO_MAXIOV)
		goto out_put;

	/* Check whether to allocate the iovec area*/
	err = -ENOMEM;
	iov_size = msg_sys.msg_iovlen * sizeof(struct iovec);
	if (msg_sys.msg_iovlen > UIO_FASTIOV) {
		iov = sock_kmalloc(sock->sk, iov_size, GFP_KERNEL);
		if (!iov)
			goto out_put;
	}

	err = verify_compat_iovec(&msg_sys, iov, address, VERIFY_READ);
	if (err < 0)
		goto out_freeiov;
	total_len = err;

	err = -ENOBUFS;
	if (msg_sys.msg_controllen > INT_MAX)
		goto out_freeiov;

	ctl_len = msg_sys.msg_controllen;
	if (ctl_len) {
		struct sol_cmsghdr __user *ucmsg = msg_sys.msg_control;
		unsigned long *kcmsg;
		compat_size_t cmlen;

		err = -EINVAL;
		if (ctl_len <= sizeof(compat_size_t))
			goto out_freeiov;

		if (ctl_len > sizeof(ctl)) {
			err = -ENOBUFS;
			ctl_buf = kmalloc(ctl_len, GFP_KERNEL);
			if (!ctl_buf)
				goto out_freeiov;
		}
		__get_user(cmlen, &ucmsg->cmsg_len);
		kcmsg = (unsigned long *) ctl_buf;
		*kcmsg++ = (unsigned long)cmlen;
		err = -EFAULT;
		if (copy_from_user(kcmsg, &ucmsg->cmsg_level,
				   ctl_len - sizeof(compat_size_t)))
			goto out_freectl;
		msg_sys.msg_control = ctl_buf;
	}
	msg_sys.msg_flags = solaris_to_linux_msgflags(user_flags);

	if (sock->file->f_flags & O_NONBLOCK)
		msg_sys.msg_flags |= MSG_DONTWAIT;
	err = sock_sendmsg(sock, &msg_sys, total_len);

out_freectl:
	if (ctl_buf != ctl)    
		sock_kfree_s(sock->sk, ctl_buf, ctl_len);
out_freeiov:
	if (iov != iovstack)
		sock_kfree_s(sock->sk, iov, iov_size);
out_put:
	sockfd_put(sock);
out:       
	return err;
}

asmlinkage int solaris_recvmsg(int fd, struct sol_nmsghdr __user *user_msg, unsigned int user_flags)
{
	struct socket *sock;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov = iovstack;
	struct msghdr msg_sys;
	unsigned long cmsg_ptr;
	int err, iov_size, total_len, len;

	/* kernel mode address */
	char addr[MAX_SOCK_ADDR];

	/* user mode address pointers */
	struct sockaddr __user *uaddr;
	int __user *uaddr_len;

	if (msghdr_from_user32_to_kern(&msg_sys, user_msg))
		return -EFAULT;

	sock = sockfd_lookup(fd, &err);
	if (!sock)
		goto out;

	err = -EMSGSIZE;
	if (msg_sys.msg_iovlen > UIO_MAXIOV)
		goto out_put;

	/* Check whether to allocate the iovec area*/
	err = -ENOMEM;
	iov_size = msg_sys.msg_iovlen * sizeof(struct iovec);
	if (msg_sys.msg_iovlen > UIO_FASTIOV) {
		iov = sock_kmalloc(sock->sk, iov_size, GFP_KERNEL);
		if (!iov)
			goto out_put;
	}

	/*
	 *	Save the user-mode address (verify_iovec will change the
	 *	kernel msghdr to use the kernel address space)
	 */
	 
	uaddr = (void __user *) msg_sys.msg_name;
	uaddr_len = &user_msg->msg_namelen;
	err = verify_compat_iovec(&msg_sys, iov, addr, VERIFY_WRITE);
	if (err < 0)
		goto out_freeiov;
	total_len = err;

	cmsg_ptr = (unsigned long) msg_sys.msg_control;
	msg_sys.msg_flags = MSG_CMSG_COMPAT;

	if (sock->file->f_flags & O_NONBLOCK)
		user_flags |= MSG_DONTWAIT;

	err = sock_recvmsg(sock, &msg_sys, total_len, user_flags);
	if(err < 0)
		goto out_freeiov;

	len = err;

	if (uaddr != NULL) {
		err = move_addr_to_user(addr, msg_sys.msg_namelen, uaddr, uaddr_len);
		if (err < 0)
			goto out_freeiov;
	}
	err = __put_user(linux_to_solaris_msgflags(msg_sys.msg_flags), &user_msg->msg_flags);
	if (err)
		goto out_freeiov;
	err = __put_user((unsigned long)msg_sys.msg_control - cmsg_ptr,
			 &user_msg->msg_controllen);
	if (err)
		goto out_freeiov;
	err = len;

out_freeiov:
	if (iov != iovstack)
		sock_kfree_s(sock->sk, iov, iov_size);
out_put:
	sockfd_put(sock);
out:
	return err;
}
