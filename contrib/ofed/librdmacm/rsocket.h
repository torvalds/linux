/*
 * Copyright (c) 2011-2012 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if !defined(RSOCKET_H)
#define RSOCKET_H

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <sys/socket.h>
#include <errno.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/mman.h>

#ifdef __cplusplus
extern "C" {
#endif

int rsocket(int domain, int type, int protocol);
int rbind(int socket, const struct sockaddr *addr, socklen_t addrlen);
int rlisten(int socket, int backlog);
int raccept(int socket, struct sockaddr *addr, socklen_t *addrlen);
int rconnect(int socket, const struct sockaddr *addr, socklen_t addrlen);
int rshutdown(int socket, int how);
int rclose(int socket);

ssize_t rrecv(int socket, void *buf, size_t len, int flags);
ssize_t rrecvfrom(int socket, void *buf, size_t len, int flags,
		  struct sockaddr *src_addr, socklen_t *addrlen);
ssize_t rrecvmsg(int socket, struct msghdr *msg, int flags);
ssize_t rsend(int socket, const void *buf, size_t len, int flags);
ssize_t rsendto(int socket, const void *buf, size_t len, int flags,
		const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t rsendmsg(int socket, const struct msghdr *msg, int flags);
ssize_t rread(int socket, void *buf, size_t count);
ssize_t rreadv(int socket, const struct iovec *iov, int iovcnt);
ssize_t rwrite(int socket, const void *buf, size_t count);
ssize_t rwritev(int socket, const struct iovec *iov, int iovcnt);

int rpoll(struct pollfd *fds, nfds_t nfds, int timeout);
int rselect(int nfds, fd_set *readfds, fd_set *writefds,
	    fd_set *exceptfds, struct timeval *timeout);

int rgetpeername(int socket, struct sockaddr *addr, socklen_t *addrlen);
int rgetsockname(int socket, struct sockaddr *addr, socklen_t *addrlen);

#define SOL_RDMA 0x10000
enum {
	RDMA_SQSIZE,
	RDMA_RQSIZE,
	RDMA_INLINE,
	RDMA_IOMAPSIZE,
	RDMA_ROUTE
};

int rsetsockopt(int socket, int level, int optname,
		const void *optval, socklen_t optlen);
int rgetsockopt(int socket, int level, int optname,
		void *optval, socklen_t *optlen);
int rfcntl(int socket, int cmd, ... /* arg */ );

off_t riomap(int socket, void *buf, size_t len, int prot, int flags, off_t offset);
int riounmap(int socket, void *buf, size_t len);
size_t riowrite(int socket, const void *buf, size_t count, off_t offset, int flags);

#ifdef __cplusplus
}
#endif

#endif /* RSOCKET_H */
