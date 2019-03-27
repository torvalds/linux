/*
 * Copyright (C) Jelmer Vernooij 2005 <jelmer@samba.org>
 * Copyright (C) Stefan Metzmacher 2006 <metze@samba.org>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef __SOCKET_WRAPPER_H__
#define __SOCKET_WRAPPER_H__

int swrap_socket(int family, int type, int protocol);
int swrap_accept(int s, struct sockaddr *addr, socklen_t *addrlen);
int swrap_connect(int s, const struct sockaddr *serv_addr, socklen_t addrlen);
int swrap_bind(int s, const struct sockaddr *myaddr, socklen_t addrlen);
int swrap_listen(int s, int backlog);
int swrap_getpeername(int s, struct sockaddr *name, socklen_t *addrlen);
int swrap_getsockname(int s, struct sockaddr *name, socklen_t *addrlen);
int swrap_getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen);
int swrap_setsockopt(int s, int  level,  int  optname,  const  void  *optval, socklen_t optlen);
ssize_t swrap_recvfrom(int s, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen);
ssize_t swrap_sendto(int s, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen);
int swrap_ioctl(int s, int req, void *ptr);
ssize_t swrap_recv(int s, void *buf, size_t len, int flags);
ssize_t swrap_send(int s, const void *buf, size_t len, int flags);
int swrap_close(int);
int swrap_dup(int);
int swrap_dup2(int, int);

#ifdef SOCKET_WRAPPER_REPLACE

#ifdef accept
#undef accept
#endif
#define accept(s,addr,addrlen)		swrap_accept(s,addr,addrlen)

#ifdef connect
#undef connect
#endif
#define connect(s,serv_addr,addrlen)	swrap_connect(s,serv_addr,addrlen)

#ifdef bind
#undef bind
#endif
#define bind(s,myaddr,addrlen)		swrap_bind(s,myaddr,addrlen)

#ifdef listen
#undef listen
#endif
#define listen(s,blog)			swrap_listen(s,blog)

#ifdef getpeername
#undef getpeername
#endif
#define getpeername(s,name,addrlen)	swrap_getpeername(s,name,addrlen)

#ifdef getsockname
#undef getsockname
#endif
#define getsockname(s,name,addrlen)	swrap_getsockname(s,name,addrlen)

#ifdef getsockopt
#undef getsockopt
#endif
#define getsockopt(s,level,optname,optval,optlen) swrap_getsockopt(s,level,optname,optval,optlen)

#ifdef setsockopt
#undef setsockopt
#endif
#define setsockopt(s,level,optname,optval,optlen) swrap_setsockopt(s,level,optname,optval,optlen)

#ifdef recvfrom
#undef recvfrom
#endif
#define recvfrom(s,buf,len,flags,from,fromlen) 	  swrap_recvfrom(s,buf,len,flags,from,fromlen)

#ifdef sendto
#undef sendto
#endif
#define sendto(s,buf,len,flags,to,tolen)          swrap_sendto(s,buf,len,flags,to,tolen)

#ifdef ioctl
#undef ioctl
#endif
#define ioctl(s,req,ptr)		swrap_ioctl(s,req,ptr)

#ifdef recv
#undef recv
#endif
#define recv(s,buf,len,flags)		swrap_recv(s,buf,len,flags)

#ifdef send
#undef send
#endif
#define send(s,buf,len,flags)		swrap_send(s,buf,len,flags)

#ifdef socket
#undef socket
#endif
#define socket(domain,type,protocol)	swrap_socket(domain,type,protocol)

#ifdef close
#undef close
#endif
#define close(s)			swrap_close(s)

#ifdef dup
#undef dup
#endif
#define dup(oldd)			swrap_dup(oldd)

#ifdef dup2
#undef dup2
#endif
#define dup2(oldd, newd)		swrap_dup2(oldd, newd)

#endif

#endif /* __SOCKET_WRAPPER_H__ */
