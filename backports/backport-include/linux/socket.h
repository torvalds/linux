#ifndef __BACKPORT_SOCKET_H
#define __BACKPORT_SOCKET_H
#include_next <linux/socket.h>

#ifndef SOL_NFC
/*
 * backport SOL_NFC -- see commit:
 * NFC: llcp: Implement socket options
 */
#define SOL_NFC		280
#endif

#ifndef __sockaddr_check_size
#define __sockaddr_check_size(size)	\
	BUILD_BUG_ON(((size) > sizeof(struct __kernel_sockaddr_storage)))
#endif

#endif /* __BACKPORT_SOCKET_H */
