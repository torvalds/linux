/*
 * util/netevent.c - event notification
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains event notification functions.
 */
#include "config.h"
#include "util/netevent.h"
#include "util/ub_event.h"
#include "util/log.h"
#include "util/net_help.h"
#include "util/tcp_conn_limit.h"
#include "util/fptr_wlist.h"
#include "sldns/pkthdr.h"
#include "sldns/sbuffer.h"
#include "sldns/str2wire.h"
#include "dnstap/dnstap.h"
#include "dnscrypt/dnscrypt.h"
#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif

/* -------- Start of local definitions -------- */
/** if CMSG_ALIGN is not defined on this platform, a workaround */
#ifndef CMSG_ALIGN
#  ifdef __CMSG_ALIGN
#    define CMSG_ALIGN(n) __CMSG_ALIGN(n)
#  elif defined(CMSG_DATA_ALIGN)
#    define CMSG_ALIGN _CMSG_DATA_ALIGN
#  else
#    define CMSG_ALIGN(len) (((len)+sizeof(long)-1) & ~(sizeof(long)-1))
#  endif
#endif

/** if CMSG_LEN is not defined on this platform, a workaround */
#ifndef CMSG_LEN
#  define CMSG_LEN(len) (CMSG_ALIGN(sizeof(struct cmsghdr))+(len))
#endif

/** if CMSG_SPACE is not defined on this platform, a workaround */
#ifndef CMSG_SPACE
#  ifdef _CMSG_HDR_ALIGN
#    define CMSG_SPACE(l) (CMSG_ALIGN(l)+_CMSG_HDR_ALIGN(sizeof(struct cmsghdr)))
#  else
#    define CMSG_SPACE(l) (CMSG_ALIGN(l)+CMSG_ALIGN(sizeof(struct cmsghdr)))
#  endif
#endif

/** The TCP writing query timeout in milliseconds */
#define TCP_QUERY_TIMEOUT 120000
/** The minimum actual TCP timeout to use, regardless of what we advertise,
 * in msec */
#define TCP_QUERY_TIMEOUT_MINIMUM 200

#ifndef NONBLOCKING_IS_BROKEN
/** number of UDP reads to perform per read indication from select */
#define NUM_UDP_PER_SELECT 100
#else
#define NUM_UDP_PER_SELECT 1
#endif

/**
 * The internal event structure for keeping ub_event info for the event.
 * Possibly other structures (list, tree) this is part of.
 */
struct internal_event {
	/** the comm base */
	struct comm_base* base;
	/** ub_event event type */
	struct ub_event* ev;
};

/**
 * Internal base structure, so that every thread has its own events.
 */
struct internal_base {
	/** ub_event event_base type. */
	struct ub_event_base* base;
	/** seconds time pointer points here */
	time_t secs;
	/** timeval with current time */
	struct timeval now;
	/** the event used for slow_accept timeouts */
	struct ub_event* slow_accept;
	/** true if slow_accept is enabled */
	int slow_accept_enabled;
};

/**
 * Internal timer structure, to store timer event in.
 */
struct internal_timer {
	/** the super struct from which derived */
	struct comm_timer super;
	/** the comm base */
	struct comm_base* base;
	/** ub_event event type */
	struct ub_event* ev;
	/** is timer enabled */
	uint8_t enabled;
};

/**
 * Internal signal structure, to store signal event in.
 */
struct internal_signal {
	/** ub_event event type */
	struct ub_event* ev;
	/** next in signal list */
	struct internal_signal* next;
};

/** create a tcp handler with a parent */
static struct comm_point* comm_point_create_tcp_handler(
	struct comm_base *base, struct comm_point* parent, size_t bufsize,
        comm_point_callback_type* callback, void* callback_arg);

/* -------- End of local definitions -------- */

struct comm_base* 
comm_base_create(int sigs)
{
	struct comm_base* b = (struct comm_base*)calloc(1,
		sizeof(struct comm_base));
	const char *evnm="event", *evsys="", *evmethod="";

	if(!b)
		return NULL;
	b->eb = (struct internal_base*)calloc(1, sizeof(struct internal_base));
	if(!b->eb) {
		free(b);
		return NULL;
	}
	b->eb->base = ub_default_event_base(sigs, &b->eb->secs, &b->eb->now);
	if(!b->eb->base) {
		free(b->eb);
		free(b);
		return NULL;
	}
	ub_comm_base_now(b);
	ub_get_event_sys(b->eb->base, &evnm, &evsys, &evmethod);
	verbose(VERB_ALGO, "%s %s user %s method.", evnm, evsys, evmethod);
	return b;
}

struct comm_base*
comm_base_create_event(struct ub_event_base* base)
{
	struct comm_base* b = (struct comm_base*)calloc(1,
		sizeof(struct comm_base));
	if(!b)
		return NULL;
	b->eb = (struct internal_base*)calloc(1, sizeof(struct internal_base));
	if(!b->eb) {
		free(b);
		return NULL;
	}
	b->eb->base = base;
	ub_comm_base_now(b);
	return b;
}

void 
comm_base_delete(struct comm_base* b)
{
	if(!b)
		return;
	if(b->eb->slow_accept_enabled) {
		if(ub_event_del(b->eb->slow_accept) != 0) {
			log_err("could not event_del slow_accept");
		}
		ub_event_free(b->eb->slow_accept);
	}
	ub_event_base_free(b->eb->base);
	b->eb->base = NULL;
	free(b->eb);
	free(b);
}

void 
comm_base_delete_no_base(struct comm_base* b)
{
	if(!b)
		return;
	if(b->eb->slow_accept_enabled) {
		if(ub_event_del(b->eb->slow_accept) != 0) {
			log_err("could not event_del slow_accept");
		}
		ub_event_free(b->eb->slow_accept);
	}
	b->eb->base = NULL;
	free(b->eb);
	free(b);
}

void 
comm_base_timept(struct comm_base* b, time_t** tt, struct timeval** tv)
{
	*tt = &b->eb->secs;
	*tv = &b->eb->now;
}

void 
comm_base_dispatch(struct comm_base* b)
{
	int retval;
	retval = ub_event_base_dispatch(b->eb->base);
	if(retval < 0) {
		fatal_exit("event_dispatch returned error %d, "
			"errno is %s", retval, strerror(errno));
	}
}

void comm_base_exit(struct comm_base* b)
{
	if(ub_event_base_loopexit(b->eb->base) != 0) {
		log_err("Could not loopexit");
	}
}

void comm_base_set_slow_accept_handlers(struct comm_base* b,
	void (*stop_acc)(void*), void (*start_acc)(void*), void* arg)
{
	b->stop_accept = stop_acc;
	b->start_accept = start_acc;
	b->cb_arg = arg;
}

struct ub_event_base* comm_base_internal(struct comm_base* b)
{
	return b->eb->base;
}

/** see if errno for udp has to be logged or not uses globals */
static int
udp_send_errno_needs_log(struct sockaddr* addr, socklen_t addrlen)
{
	/* do not log transient errors (unless high verbosity) */
#if defined(ENETUNREACH) || defined(EHOSTDOWN) || defined(EHOSTUNREACH) || defined(ENETDOWN)
	switch(errno) {
#  ifdef ENETUNREACH
		case ENETUNREACH:
#  endif
#  ifdef EHOSTDOWN
		case EHOSTDOWN:
#  endif
#  ifdef EHOSTUNREACH
		case EHOSTUNREACH:
#  endif
#  ifdef ENETDOWN
		case ENETDOWN:
#  endif
			if(verbosity < VERB_ALGO)
				return 0;
		default:
			break;
	}
#endif
	/* permission denied is gotten for every send if the
	 * network is disconnected (on some OS), squelch it */
	if( ((errno == EPERM)
#  ifdef EADDRNOTAVAIL
		/* 'Cannot assign requested address' also when disconnected */
		|| (errno == EADDRNOTAVAIL)
#  endif
		) && verbosity < VERB_DETAIL)
		return 0;
#  ifdef EADDRINUSE
	/* If SO_REUSEADDR is set, we could try to connect to the same server
	 * from the same source port twice. */
	if(errno == EADDRINUSE && verbosity < VERB_DETAIL)
		return 0;
#  endif
	/* squelch errors where people deploy AAAA ::ffff:bla for
	 * authority servers, which we try for intranets. */
	if(errno == EINVAL && addr_is_ip4mapped(
		(struct sockaddr_storage*)addr, addrlen) &&
		verbosity < VERB_DETAIL)
		return 0;
	/* SO_BROADCAST sockopt can give access to 255.255.255.255,
	 * but a dns cache does not need it. */
	if(errno == EACCES && addr_is_broadcast(
		(struct sockaddr_storage*)addr, addrlen) &&
		verbosity < VERB_DETAIL)
		return 0;
	return 1;
}

int tcp_connect_errno_needs_log(struct sockaddr* addr, socklen_t addrlen)
{
	return udp_send_errno_needs_log(addr, addrlen);
}

/* send a UDP reply */
int
comm_point_send_udp_msg(struct comm_point *c, sldns_buffer* packet,
	struct sockaddr* addr, socklen_t addrlen) 
{
	ssize_t sent;
	log_assert(c->fd != -1);
#ifdef UNBOUND_DEBUG
	if(sldns_buffer_remaining(packet) == 0)
		log_err("error: send empty UDP packet");
#endif
	log_assert(addr && addrlen > 0);
	sent = sendto(c->fd, (void*)sldns_buffer_begin(packet), 
		sldns_buffer_remaining(packet), 0,
		addr, addrlen);
	if(sent == -1) {
		/* try again and block, waiting for IO to complete,
		 * we want to send the answer, and we will wait for
		 * the ethernet interface buffer to have space. */
#ifndef USE_WINSOCK
		if(errno == EAGAIN || 
#  ifdef EWOULDBLOCK
			errno == EWOULDBLOCK ||
#  endif
			errno == ENOBUFS) {
#else
		if(WSAGetLastError() == WSAEINPROGRESS ||
			WSAGetLastError() == WSAENOBUFS ||
			WSAGetLastError() == WSAEWOULDBLOCK) {
#endif
			int e;
			fd_set_block(c->fd);
			sent = sendto(c->fd, (void*)sldns_buffer_begin(packet), 
				sldns_buffer_remaining(packet), 0,
				addr, addrlen);
			e = errno;
			fd_set_nonblock(c->fd);
			errno = e;
		}
	}
	if(sent == -1) {
		if(!udp_send_errno_needs_log(addr, addrlen))
			return 0;
#ifndef USE_WINSOCK
		verbose(VERB_OPS, "sendto failed: %s", strerror(errno));
#else
		verbose(VERB_OPS, "sendto failed: %s", 
			wsa_strerror(WSAGetLastError()));
#endif
		log_addr(VERB_OPS, "remote address is", 
			(struct sockaddr_storage*)addr, addrlen);
		return 0;
	} else if((size_t)sent != sldns_buffer_remaining(packet)) {
		log_err("sent %d in place of %d bytes", 
			(int)sent, (int)sldns_buffer_remaining(packet));
		return 0;
	}
	return 1;
}

#if defined(AF_INET6) && defined(IPV6_PKTINFO) && (defined(HAVE_RECVMSG) || defined(HAVE_SENDMSG))
/** print debug ancillary info */
static void p_ancil(const char* str, struct comm_reply* r)
{
	if(r->srctype != 4 && r->srctype != 6) {
		log_info("%s: unknown srctype %d", str, r->srctype);
		return;
	}
	if(r->srctype == 6) {
		char buf[1024];
		if(inet_ntop(AF_INET6, &r->pktinfo.v6info.ipi6_addr, 
			buf, (socklen_t)sizeof(buf)) == 0) {
			(void)strlcpy(buf, "(inet_ntop error)", sizeof(buf));
		}
		buf[sizeof(buf)-1]=0;
		log_info("%s: %s %d", str, buf, r->pktinfo.v6info.ipi6_ifindex);
	} else if(r->srctype == 4) {
#ifdef IP_PKTINFO
		char buf1[1024], buf2[1024];
		if(inet_ntop(AF_INET, &r->pktinfo.v4info.ipi_addr, 
			buf1, (socklen_t)sizeof(buf1)) == 0) {
			(void)strlcpy(buf1, "(inet_ntop error)", sizeof(buf1));
		}
		buf1[sizeof(buf1)-1]=0;
#ifdef HAVE_STRUCT_IN_PKTINFO_IPI_SPEC_DST
		if(inet_ntop(AF_INET, &r->pktinfo.v4info.ipi_spec_dst, 
			buf2, (socklen_t)sizeof(buf2)) == 0) {
			(void)strlcpy(buf2, "(inet_ntop error)", sizeof(buf2));
		}
		buf2[sizeof(buf2)-1]=0;
#else
		buf2[0]=0;
#endif
		log_info("%s: %d %s %s", str, r->pktinfo.v4info.ipi_ifindex,
			buf1, buf2);
#elif defined(IP_RECVDSTADDR)
		char buf1[1024];
		if(inet_ntop(AF_INET, &r->pktinfo.v4addr, 
			buf1, (socklen_t)sizeof(buf1)) == 0) {
			(void)strlcpy(buf1, "(inet_ntop error)", sizeof(buf1));
		}
		buf1[sizeof(buf1)-1]=0;
		log_info("%s: %s", str, buf1);
#endif /* IP_PKTINFO or PI_RECVDSTDADDR */
	}
}
#endif /* AF_INET6 && IPV6_PKTINFO && HAVE_RECVMSG||HAVE_SENDMSG */

/** send a UDP reply over specified interface*/
static int
comm_point_send_udp_msg_if(struct comm_point *c, sldns_buffer* packet,
	struct sockaddr* addr, socklen_t addrlen, struct comm_reply* r) 
{
#if defined(AF_INET6) && defined(IPV6_PKTINFO) && defined(HAVE_SENDMSG)
	ssize_t sent;
	struct msghdr msg;
	struct iovec iov[1];
	char control[256];
#ifndef S_SPLINT_S
	struct cmsghdr *cmsg;
#endif /* S_SPLINT_S */

	log_assert(c->fd != -1);
#ifdef UNBOUND_DEBUG
	if(sldns_buffer_remaining(packet) == 0)
		log_err("error: send empty UDP packet");
#endif
	log_assert(addr && addrlen > 0);

	msg.msg_name = addr;
	msg.msg_namelen = addrlen;
	iov[0].iov_base = sldns_buffer_begin(packet);
	iov[0].iov_len = sldns_buffer_remaining(packet);
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control;
#ifndef S_SPLINT_S
	msg.msg_controllen = sizeof(control);
#endif /* S_SPLINT_S */
	msg.msg_flags = 0;

#ifndef S_SPLINT_S
	cmsg = CMSG_FIRSTHDR(&msg);
	if(r->srctype == 4) {
#ifdef IP_PKTINFO
		void* cmsg_data;
		msg.msg_controllen = CMSG_SPACE(sizeof(struct in_pktinfo));
		log_assert(msg.msg_controllen <= sizeof(control));
		cmsg->cmsg_level = IPPROTO_IP;
		cmsg->cmsg_type = IP_PKTINFO;
		memmove(CMSG_DATA(cmsg), &r->pktinfo.v4info,
			sizeof(struct in_pktinfo));
		/* unset the ifindex to not bypass the routing tables */
		cmsg_data = CMSG_DATA(cmsg);
		((struct in_pktinfo *) cmsg_data)->ipi_ifindex = 0;
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in_pktinfo));
#elif defined(IP_SENDSRCADDR)
		msg.msg_controllen = CMSG_SPACE(sizeof(struct in_addr));
		log_assert(msg.msg_controllen <= sizeof(control));
		cmsg->cmsg_level = IPPROTO_IP;
		cmsg->cmsg_type = IP_SENDSRCADDR;
		memmove(CMSG_DATA(cmsg), &r->pktinfo.v4addr,
			sizeof(struct in_addr));
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in_addr));
#else
		verbose(VERB_ALGO, "no IP_PKTINFO or IP_SENDSRCADDR");
		msg.msg_control = NULL;
#endif /* IP_PKTINFO or IP_SENDSRCADDR */
	} else if(r->srctype == 6) {
		void* cmsg_data;
		msg.msg_controllen = CMSG_SPACE(sizeof(struct in6_pktinfo));
		log_assert(msg.msg_controllen <= sizeof(control));
		cmsg->cmsg_level = IPPROTO_IPV6;
		cmsg->cmsg_type = IPV6_PKTINFO;
		memmove(CMSG_DATA(cmsg), &r->pktinfo.v6info,
			sizeof(struct in6_pktinfo));
		/* unset the ifindex to not bypass the routing tables */
		cmsg_data = CMSG_DATA(cmsg);
		((struct in6_pktinfo *) cmsg_data)->ipi6_ifindex = 0;
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
	} else {
		/* try to pass all 0 to use default route */
		msg.msg_controllen = CMSG_SPACE(sizeof(struct in6_pktinfo));
		log_assert(msg.msg_controllen <= sizeof(control));
		cmsg->cmsg_level = IPPROTO_IPV6;
		cmsg->cmsg_type = IPV6_PKTINFO;
		memset(CMSG_DATA(cmsg), 0, sizeof(struct in6_pktinfo));
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
	}
#endif /* S_SPLINT_S */
	if(verbosity >= VERB_ALGO)
		p_ancil("send_udp over interface", r);
	sent = sendmsg(c->fd, &msg, 0);
	if(sent == -1) {
		/* try again and block, waiting for IO to complete,
		 * we want to send the answer, and we will wait for
		 * the ethernet interface buffer to have space. */
#ifndef USE_WINSOCK
		if(errno == EAGAIN || 
#  ifdef EWOULDBLOCK
			errno == EWOULDBLOCK ||
#  endif
			errno == ENOBUFS) {
#else
		if(WSAGetLastError() == WSAEINPROGRESS ||
			WSAGetLastError() == WSAENOBUFS ||
			WSAGetLastError() == WSAEWOULDBLOCK) {
#endif
			int e;
			fd_set_block(c->fd);
			sent = sendmsg(c->fd, &msg, 0);
			e = errno;
			fd_set_nonblock(c->fd);
			errno = e;
		}
	}
	if(sent == -1) {
		if(!udp_send_errno_needs_log(addr, addrlen))
			return 0;
		verbose(VERB_OPS, "sendmsg failed: %s", strerror(errno));
		log_addr(VERB_OPS, "remote address is", 
			(struct sockaddr_storage*)addr, addrlen);
#ifdef __NetBSD__
		/* netbsd 7 has IP_PKTINFO for recv but not send */
		if(errno == EINVAL && r->srctype == 4)
			log_err("sendmsg: No support for sendmsg(IP_PKTINFO). "
				"Please disable interface-automatic");
#endif
		return 0;
	} else if((size_t)sent != sldns_buffer_remaining(packet)) {
		log_err("sent %d in place of %d bytes", 
			(int)sent, (int)sldns_buffer_remaining(packet));
		return 0;
	}
	return 1;
#else
	(void)c;
	(void)packet;
	(void)addr;
	(void)addrlen;
	(void)r;
	log_err("sendmsg: IPV6_PKTINFO not supported");
	return 0;
#endif /* AF_INET6 && IPV6_PKTINFO && HAVE_SENDMSG */
}

void 
comm_point_udp_ancil_callback(int fd, short event, void* arg)
{
#if defined(AF_INET6) && defined(IPV6_PKTINFO) && defined(HAVE_RECVMSG)
	struct comm_reply rep;
	struct msghdr msg;
	struct iovec iov[1];
	ssize_t rcv;
	char ancil[256];
	int i;
#ifndef S_SPLINT_S
	struct cmsghdr* cmsg;
#endif /* S_SPLINT_S */

	rep.c = (struct comm_point*)arg;
	log_assert(rep.c->type == comm_udp);

	if(!(event&UB_EV_READ))
		return;
	log_assert(rep.c && rep.c->buffer && rep.c->fd == fd);
	ub_comm_base_now(rep.c->ev->base);
	for(i=0; i<NUM_UDP_PER_SELECT; i++) {
		sldns_buffer_clear(rep.c->buffer);
		rep.addrlen = (socklen_t)sizeof(rep.addr);
		log_assert(fd != -1);
		log_assert(sldns_buffer_remaining(rep.c->buffer) > 0);
		msg.msg_name = &rep.addr;
		msg.msg_namelen = (socklen_t)sizeof(rep.addr);
		iov[0].iov_base = sldns_buffer_begin(rep.c->buffer);
		iov[0].iov_len = sldns_buffer_remaining(rep.c->buffer);
		msg.msg_iov = iov;
		msg.msg_iovlen = 1;
		msg.msg_control = ancil;
#ifndef S_SPLINT_S
		msg.msg_controllen = sizeof(ancil);
#endif /* S_SPLINT_S */
		msg.msg_flags = 0;
		rcv = recvmsg(fd, &msg, 0);
		if(rcv == -1) {
			if(errno != EAGAIN && errno != EINTR) {
				log_err("recvmsg failed: %s", strerror(errno));
			}
			return;
		}
		rep.addrlen = msg.msg_namelen;
		sldns_buffer_skip(rep.c->buffer, rcv);
		sldns_buffer_flip(rep.c->buffer);
		rep.srctype = 0;
#ifndef S_SPLINT_S
		for(cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
			cmsg = CMSG_NXTHDR(&msg, cmsg)) {
			if( cmsg->cmsg_level == IPPROTO_IPV6 &&
				cmsg->cmsg_type == IPV6_PKTINFO) {
				rep.srctype = 6;
				memmove(&rep.pktinfo.v6info, CMSG_DATA(cmsg),
					sizeof(struct in6_pktinfo));
				break;
#ifdef IP_PKTINFO
			} else if( cmsg->cmsg_level == IPPROTO_IP &&
				cmsg->cmsg_type == IP_PKTINFO) {
				rep.srctype = 4;
				memmove(&rep.pktinfo.v4info, CMSG_DATA(cmsg),
					sizeof(struct in_pktinfo));
				break;
#elif defined(IP_RECVDSTADDR)
			} else if( cmsg->cmsg_level == IPPROTO_IP &&
				cmsg->cmsg_type == IP_RECVDSTADDR) {
				rep.srctype = 4;
				memmove(&rep.pktinfo.v4addr, CMSG_DATA(cmsg),
					sizeof(struct in_addr));
				break;
#endif /* IP_PKTINFO or IP_RECVDSTADDR */
			}
		}
		if(verbosity >= VERB_ALGO)
			p_ancil("receive_udp on interface", &rep);
#endif /* S_SPLINT_S */
		fptr_ok(fptr_whitelist_comm_point(rep.c->callback));
		if((*rep.c->callback)(rep.c, rep.c->cb_arg, NETEVENT_NOERROR, &rep)) {
			/* send back immediate reply */
			(void)comm_point_send_udp_msg_if(rep.c, rep.c->buffer,
				(struct sockaddr*)&rep.addr, rep.addrlen, &rep);
		}
		if(!rep.c || rep.c->fd == -1) /* commpoint closed */
			break;
	}
#else
	(void)fd;
	(void)event;
	(void)arg;
	fatal_exit("recvmsg: No support for IPV6_PKTINFO; IP_PKTINFO or IP_RECVDSTADDR. "
		"Please disable interface-automatic");
#endif /* AF_INET6 && IPV6_PKTINFO && HAVE_RECVMSG */
}

void 
comm_point_udp_callback(int fd, short event, void* arg)
{
	struct comm_reply rep;
	ssize_t rcv;
	int i;
	struct sldns_buffer *buffer;

	rep.c = (struct comm_point*)arg;
	log_assert(rep.c->type == comm_udp);

	if(!(event&UB_EV_READ))
		return;
	log_assert(rep.c && rep.c->buffer && rep.c->fd == fd);
	ub_comm_base_now(rep.c->ev->base);
	for(i=0; i<NUM_UDP_PER_SELECT; i++) {
		sldns_buffer_clear(rep.c->buffer);
		rep.addrlen = (socklen_t)sizeof(rep.addr);
		log_assert(fd != -1);
		log_assert(sldns_buffer_remaining(rep.c->buffer) > 0);
		rcv = recvfrom(fd, (void*)sldns_buffer_begin(rep.c->buffer), 
			sldns_buffer_remaining(rep.c->buffer), 0, 
			(struct sockaddr*)&rep.addr, &rep.addrlen);
		if(rcv == -1) {
#ifndef USE_WINSOCK
			if(errno != EAGAIN && errno != EINTR)
				log_err("recvfrom %d failed: %s", 
					fd, strerror(errno));
#else
			if(WSAGetLastError() != WSAEINPROGRESS &&
				WSAGetLastError() != WSAECONNRESET &&
				WSAGetLastError()!= WSAEWOULDBLOCK)
				log_err("recvfrom failed: %s",
					wsa_strerror(WSAGetLastError()));
#endif
			return;
		}
		sldns_buffer_skip(rep.c->buffer, rcv);
		sldns_buffer_flip(rep.c->buffer);
		rep.srctype = 0;
		fptr_ok(fptr_whitelist_comm_point(rep.c->callback));
		if((*rep.c->callback)(rep.c, rep.c->cb_arg, NETEVENT_NOERROR, &rep)) {
			/* send back immediate reply */
#ifdef USE_DNSCRYPT
			buffer = rep.c->dnscrypt_buffer;
#else
			buffer = rep.c->buffer;
#endif
			(void)comm_point_send_udp_msg(rep.c, buffer,
				(struct sockaddr*)&rep.addr, rep.addrlen);
		}
		if(!rep.c || rep.c->fd != fd) /* commpoint closed to -1 or reused for
		another UDP port. Note rep.c cannot be reused with TCP fd. */
			break;
	}
}

/** Use a new tcp handler for new query fd, set to read query */
static void
setup_tcp_handler(struct comm_point* c, int fd, int cur, int max) 
{
	int handler_usage;
	log_assert(c->type == comm_tcp);
	log_assert(c->fd == -1);
	sldns_buffer_clear(c->buffer);
#ifdef USE_DNSCRYPT
	if (c->dnscrypt)
		sldns_buffer_clear(c->dnscrypt_buffer);
#endif
	c->tcp_is_reading = 1;
	c->tcp_byte_count = 0;
	/* if more than half the tcp handlers are in use, use a shorter
	 * timeout for this TCP connection, we need to make space for
	 * other connections to be able to get attention */
	/* If > 50% TCP handler structures in use, set timeout to 1/100th
	 * 	configured value.
	 * If > 65%TCP handler structures in use, set to 1/500th configured
	 * 	value.
	 * If > 80% TCP handler structures in use, set to 0.
	 *
	 * If the timeout to use falls below 200 milliseconds, an actual
	 * timeout of 200ms is used.
	 */
	handler_usage = (cur * 100) / max;
	if(handler_usage > 50 && handler_usage <= 65)
		c->tcp_timeout_msec /= 100;
	else if (handler_usage > 65 && handler_usage <= 80)
		c->tcp_timeout_msec /= 500;
	else if (handler_usage > 80)
		c->tcp_timeout_msec = 0;
	comm_point_start_listening(c, fd,
		c->tcp_timeout_msec < TCP_QUERY_TIMEOUT_MINIMUM
			? TCP_QUERY_TIMEOUT_MINIMUM
			: c->tcp_timeout_msec);
}

void comm_base_handle_slow_accept(int ATTR_UNUSED(fd),
	short ATTR_UNUSED(event), void* arg)
{
	struct comm_base* b = (struct comm_base*)arg;
	/* timeout for the slow accept, re-enable accepts again */
	if(b->start_accept) {
		verbose(VERB_ALGO, "wait is over, slow accept disabled");
		fptr_ok(fptr_whitelist_start_accept(b->start_accept));
		(*b->start_accept)(b->cb_arg);
		b->eb->slow_accept_enabled = 0;
	}
}

int comm_point_perform_accept(struct comm_point* c,
	struct sockaddr_storage* addr, socklen_t* addrlen)
{
	int new_fd;
	*addrlen = (socklen_t)sizeof(*addr);
#ifndef HAVE_ACCEPT4
	new_fd = accept(c->fd, (struct sockaddr*)addr, addrlen);
#else
	/* SOCK_NONBLOCK saves extra calls to fcntl for the same result */
	new_fd = accept4(c->fd, (struct sockaddr*)addr, addrlen, SOCK_NONBLOCK);
#endif
	if(new_fd == -1) {
#ifndef USE_WINSOCK
		/* EINTR is signal interrupt. others are closed connection. */
		if(	errno == EINTR || errno == EAGAIN
#ifdef EWOULDBLOCK
			|| errno == EWOULDBLOCK 
#endif
#ifdef ECONNABORTED
			|| errno == ECONNABORTED 
#endif
#ifdef EPROTO
			|| errno == EPROTO
#endif /* EPROTO */
			)
			return -1;
#if defined(ENFILE) && defined(EMFILE)
		if(errno == ENFILE || errno == EMFILE) {
			/* out of file descriptors, likely outside of our
			 * control. stop accept() calls for some time */
			if(c->ev->base->stop_accept) {
				struct comm_base* b = c->ev->base;
				struct timeval tv;
				verbose(VERB_ALGO, "out of file descriptors: "
					"slow accept");
				b->eb->slow_accept_enabled = 1;
				fptr_ok(fptr_whitelist_stop_accept(
					b->stop_accept));
				(*b->stop_accept)(b->cb_arg);
				/* set timeout, no mallocs */
				tv.tv_sec = NETEVENT_SLOW_ACCEPT_TIME/1000;
				tv.tv_usec = (NETEVENT_SLOW_ACCEPT_TIME%1000)*1000;
				b->eb->slow_accept = ub_event_new(b->eb->base,
					-1, UB_EV_TIMEOUT,
					comm_base_handle_slow_accept, b);
				if(b->eb->slow_accept == NULL) {
					/* we do not want to log here, because
					 * that would spam the logfiles.
					 * error: "event_base_set failed." */
				}
				else if(ub_event_add(b->eb->slow_accept, &tv)
					!= 0) {
					/* we do not want to log here,
					 * error: "event_add failed." */
				}
			}
			return -1;
		}
#endif
		log_err_addr("accept failed", strerror(errno), addr, *addrlen);
#else /* USE_WINSOCK */
		if(WSAGetLastError() == WSAEINPROGRESS ||
			WSAGetLastError() == WSAECONNRESET)
			return -1;
		if(WSAGetLastError() == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_READ);
			return -1;
		}
		log_err_addr("accept failed", wsa_strerror(WSAGetLastError()),
			addr, *addrlen);
#endif
		return -1;
	}
	if(c->tcp_conn_limit && c->type == comm_tcp_accept) {
		c->tcl_addr = tcl_addr_lookup(c->tcp_conn_limit, addr, *addrlen);
		if(!tcl_new_connection(c->tcl_addr)) {
			if(verbosity >= 3)
				log_err_addr("accept rejected",
				"connection limit exceeded", addr, *addrlen);
			close(new_fd);
			return -1;
		}
	}
#ifndef HAVE_ACCEPT4
	fd_set_nonblock(new_fd);
#endif
	return new_fd;
}

#ifdef USE_WINSOCK
static long win_bio_cb(BIO *b, int oper, const char* ATTR_UNUSED(argp),
        int ATTR_UNUSED(argi), long argl, long retvalue)
{
	int wsa_err = WSAGetLastError(); /* store errcode before it is gone */
	verbose(VERB_ALGO, "bio_cb %d, %s %s %s", oper,
		(oper&BIO_CB_RETURN)?"return":"before",
		(oper&BIO_CB_READ)?"read":((oper&BIO_CB_WRITE)?"write":"other"),
		wsa_err==WSAEWOULDBLOCK?"wsawb":"");
	/* on windows, check if previous operation caused EWOULDBLOCK */
	if( (oper == (BIO_CB_READ|BIO_CB_RETURN) && argl == 0) ||
		(oper == (BIO_CB_GETS|BIO_CB_RETURN) && argl == 0)) {
		if(wsa_err == WSAEWOULDBLOCK)
			ub_winsock_tcp_wouldblock((struct ub_event*)
				BIO_get_callback_arg(b), UB_EV_READ);
	}
	if( (oper == (BIO_CB_WRITE|BIO_CB_RETURN) && argl == 0) ||
		(oper == (BIO_CB_PUTS|BIO_CB_RETURN) && argl == 0)) {
		if(wsa_err == WSAEWOULDBLOCK)
			ub_winsock_tcp_wouldblock((struct ub_event*)
				BIO_get_callback_arg(b), UB_EV_WRITE);
	}
	/* return original return value */
	return retvalue;
}

/** set win bio callbacks for nonblocking operations */
void
comm_point_tcp_win_bio_cb(struct comm_point* c, void* thessl)
{
	SSL* ssl = (SSL*)thessl;
	/* set them both just in case, but usually they are the same BIO */
	BIO_set_callback(SSL_get_rbio(ssl), &win_bio_cb);
	BIO_set_callback_arg(SSL_get_rbio(ssl), (char*)c->ev->ev);
	BIO_set_callback(SSL_get_wbio(ssl), &win_bio_cb);
	BIO_set_callback_arg(SSL_get_wbio(ssl), (char*)c->ev->ev);
}
#endif

void 
comm_point_tcp_accept_callback(int fd, short event, void* arg)
{
	struct comm_point* c = (struct comm_point*)arg, *c_hdl;
	int new_fd;
	log_assert(c->type == comm_tcp_accept);
	if(!(event & UB_EV_READ)) {
		log_info("ignoring tcp accept event %d", (int)event);
		return;
	}
	ub_comm_base_now(c->ev->base);
	/* find free tcp handler. */
	if(!c->tcp_free) {
		log_warn("accepted too many tcp, connections full");
		return;
	}
	/* accept incoming connection. */
	c_hdl = c->tcp_free;
	log_assert(fd != -1);
	(void)fd;
	new_fd = comm_point_perform_accept(c, &c_hdl->repinfo.addr,
		&c_hdl->repinfo.addrlen);
	if(new_fd == -1)
		return;
	if(c->ssl) {
		c_hdl->ssl = incoming_ssl_fd(c->ssl, new_fd);
		if(!c_hdl->ssl) {
			c_hdl->fd = new_fd;
			comm_point_close(c_hdl);
			return;
		}
		c_hdl->ssl_shake_state = comm_ssl_shake_read;
#ifdef USE_WINSOCK
		comm_point_tcp_win_bio_cb(c_hdl, c_hdl->ssl);
#endif
	}

	/* grab the tcp handler buffers */
	c->cur_tcp_count++;
	c->tcp_free = c_hdl->tcp_free;
	if(!c->tcp_free) {
		/* stop accepting incoming queries for now. */
		comm_point_stop_listening(c);
	}
	setup_tcp_handler(c_hdl, new_fd, c->cur_tcp_count, c->max_tcp_count);
}

/** Make tcp handler free for next assignment */
static void
reclaim_tcp_handler(struct comm_point* c)
{
	log_assert(c->type == comm_tcp);
	if(c->ssl) {
#ifdef HAVE_SSL
		SSL_shutdown(c->ssl);
		SSL_free(c->ssl);
		c->ssl = NULL;
#endif
	}
	comm_point_close(c);
	if(c->tcp_parent) {
		c->tcp_parent->cur_tcp_count--;
		c->tcp_free = c->tcp_parent->tcp_free;
		c->tcp_parent->tcp_free = c;
		if(!c->tcp_free) {
			/* re-enable listening on accept socket */
			comm_point_start_listening(c->tcp_parent, -1, -1);
		}
	}
}

/** do the callback when writing is done */
static void
tcp_callback_writer(struct comm_point* c)
{
	log_assert(c->type == comm_tcp);
	sldns_buffer_clear(c->buffer);
	if(c->tcp_do_toggle_rw)
		c->tcp_is_reading = 1;
	c->tcp_byte_count = 0;
	/* switch from listening(write) to listening(read) */
	comm_point_stop_listening(c);
	comm_point_start_listening(c, -1, -1);
}

/** do the callback when reading is done */
static void
tcp_callback_reader(struct comm_point* c)
{
	log_assert(c->type == comm_tcp || c->type == comm_local);
	sldns_buffer_flip(c->buffer);
	if(c->tcp_do_toggle_rw)
		c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	if(c->type == comm_tcp)
		comm_point_stop_listening(c);
	fptr_ok(fptr_whitelist_comm_point(c->callback));
	if( (*c->callback)(c, c->cb_arg, NETEVENT_NOERROR, &c->repinfo) ) {
		comm_point_start_listening(c, -1, c->tcp_timeout_msec);
	}
}

#ifdef HAVE_SSL
/** log certificate details */
static void
log_cert(unsigned level, const char* str, X509* cert)
{
	BIO* bio;
	char nul = 0;
	char* pp = NULL;
	long len;
	if(verbosity < level) return;
	bio = BIO_new(BIO_s_mem());
	if(!bio) return;
	X509_print_ex(bio, cert, 0, (unsigned long)-1
		^(X509_FLAG_NO_SUBJECT
                        |X509_FLAG_NO_ISSUER|X509_FLAG_NO_VALIDITY
			|X509_FLAG_NO_EXTENSIONS|X509_FLAG_NO_AUX
			|X509_FLAG_NO_ATTRIBUTES));
	BIO_write(bio, &nul, (int)sizeof(nul));
	len = BIO_get_mem_data(bio, &pp);
	if(len != 0 && pp) {
		verbose(level, "%s: \n%s", str, pp);
	}
	BIO_free(bio);
}
#endif /* HAVE_SSL */

/** continue ssl handshake */
#ifdef HAVE_SSL
static int
ssl_handshake(struct comm_point* c)
{
	int r;
	if(c->ssl_shake_state == comm_ssl_shake_hs_read) {
		/* read condition satisfied back to writing */
		comm_point_listen_for_rw(c, 1, 1);
		c->ssl_shake_state = comm_ssl_shake_none;
		return 1;
	}
	if(c->ssl_shake_state == comm_ssl_shake_hs_write) {
		/* write condition satisfied, back to reading */
		comm_point_listen_for_rw(c, 1, 0);
		c->ssl_shake_state = comm_ssl_shake_none;
		return 1;
	}

	ERR_clear_error();
	r = SSL_do_handshake(c->ssl);
	if(r != 1) {
		int want = SSL_get_error(c->ssl, r);
		if(want == SSL_ERROR_WANT_READ) {
			if(c->ssl_shake_state == comm_ssl_shake_read)
				return 1;
			c->ssl_shake_state = comm_ssl_shake_read;
			comm_point_listen_for_rw(c, 1, 0);
			return 1;
		} else if(want == SSL_ERROR_WANT_WRITE) {
			if(c->ssl_shake_state == comm_ssl_shake_write)
				return 1;
			c->ssl_shake_state = comm_ssl_shake_write;
			comm_point_listen_for_rw(c, 0, 1);
			return 1;
		} else if(r == 0) {
			return 0; /* closed */
		} else if(want == SSL_ERROR_SYSCALL) {
			/* SYSCALL and errno==0 means closed uncleanly */
			if(errno != 0)
				log_err("SSL_handshake syscall: %s",
					strerror(errno));
			return 0;
		} else {
			log_crypto_err("ssl handshake failed");
			log_addr(1, "ssl handshake failed", &c->repinfo.addr,
				c->repinfo.addrlen);
			return 0;
		}
	}
	/* this is where peer verification could take place */
	if((SSL_get_verify_mode(c->ssl)&SSL_VERIFY_PEER)) {
		/* verification */
		if(SSL_get_verify_result(c->ssl) == X509_V_OK) {
			X509* x = SSL_get_peer_certificate(c->ssl);
			if(!x) {
				log_addr(VERB_ALGO, "SSL connection failed: "
					"no certificate",
					&c->repinfo.addr, c->repinfo.addrlen);
				return 0;
			}
			log_cert(VERB_ALGO, "peer certificate", x);
#ifdef HAVE_SSL_GET0_PEERNAME
			if(SSL_get0_peername(c->ssl)) {
				char buf[255];
				snprintf(buf, sizeof(buf), "SSL connection "
					"to %s authenticated",
					SSL_get0_peername(c->ssl));
				log_addr(VERB_ALGO, buf, &c->repinfo.addr,
					c->repinfo.addrlen);
			} else {
#endif
				log_addr(VERB_ALGO, "SSL connection "
					"authenticated", &c->repinfo.addr,
					c->repinfo.addrlen);
#ifdef HAVE_SSL_GET0_PEERNAME
			}
#endif
			X509_free(x);
		} else {
			X509* x = SSL_get_peer_certificate(c->ssl);
			if(x) {
				log_cert(VERB_ALGO, "peer certificate", x);
				X509_free(x);
			}
			log_addr(VERB_ALGO, "SSL connection failed: "
				"failed to authenticate",
				&c->repinfo.addr, c->repinfo.addrlen);
			return 0;
		}
	} else {
		/* unauthenticated, the verify peer flag was not set
		 * in c->ssl when the ssl object was created from ssl_ctx */
		log_addr(VERB_ALGO, "SSL connection", &c->repinfo.addr,
			c->repinfo.addrlen);
	}

	/* setup listen rw correctly */
	if(c->tcp_is_reading) {
		if(c->ssl_shake_state != comm_ssl_shake_read)
			comm_point_listen_for_rw(c, 1, 0);
	} else {
		comm_point_listen_for_rw(c, 1, 1);
	}
	c->ssl_shake_state = comm_ssl_shake_none;
	return 1;
}
#endif /* HAVE_SSL */

/** ssl read callback on TCP */
static int
ssl_handle_read(struct comm_point* c)
{
#ifdef HAVE_SSL
	int r;
	if(c->ssl_shake_state != comm_ssl_shake_none) {
		if(!ssl_handshake(c))
			return 0;
		if(c->ssl_shake_state != comm_ssl_shake_none)
			return 1;
	}
	if(c->tcp_byte_count < sizeof(uint16_t)) {
		/* read length bytes */
		ERR_clear_error();
		if((r=SSL_read(c->ssl, (void*)sldns_buffer_at(c->buffer,
			c->tcp_byte_count), (int)(sizeof(uint16_t) -
			c->tcp_byte_count))) <= 0) {
			int want = SSL_get_error(c->ssl, r);
			if(want == SSL_ERROR_ZERO_RETURN) {
				return 0; /* shutdown, closed */
			} else if(want == SSL_ERROR_WANT_READ) {
				ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_READ);
				return 1; /* read more later */
			} else if(want == SSL_ERROR_WANT_WRITE) {
				c->ssl_shake_state = comm_ssl_shake_hs_write;
				comm_point_listen_for_rw(c, 0, 1);
				return 1;
			} else if(want == SSL_ERROR_SYSCALL) {
				if(errno != 0)
					log_err("SSL_read syscall: %s",
						strerror(errno));
				return 0;
			}
			log_crypto_err("could not SSL_read");
			return 0;
		}
		c->tcp_byte_count += r;
		if(c->tcp_byte_count < sizeof(uint16_t))
			return 1;
		if(sldns_buffer_read_u16_at(c->buffer, 0) >
			sldns_buffer_capacity(c->buffer)) {
			verbose(VERB_QUERY, "ssl: dropped larger than buffer");
			return 0;
		}
		sldns_buffer_set_limit(c->buffer,
			sldns_buffer_read_u16_at(c->buffer, 0));
		if(sldns_buffer_limit(c->buffer) < LDNS_HEADER_SIZE) {
			verbose(VERB_QUERY, "ssl: dropped bogus too short.");
			return 0;
		}
		sldns_buffer_skip(c->buffer, (ssize_t)(c->tcp_byte_count-sizeof(uint16_t)));
		verbose(VERB_ALGO, "Reading ssl tcp query of length %d",
			(int)sldns_buffer_limit(c->buffer));
	}
	if(sldns_buffer_remaining(c->buffer) > 0) {
		ERR_clear_error();
		r = SSL_read(c->ssl, (void*)sldns_buffer_current(c->buffer),
			(int)sldns_buffer_remaining(c->buffer));
		if(r <= 0) {
			int want = SSL_get_error(c->ssl, r);
			if(want == SSL_ERROR_ZERO_RETURN) {
				return 0; /* shutdown, closed */
			} else if(want == SSL_ERROR_WANT_READ) {
				ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_READ);
				return 1; /* read more later */
			} else if(want == SSL_ERROR_WANT_WRITE) {
				c->ssl_shake_state = comm_ssl_shake_hs_write;
				comm_point_listen_for_rw(c, 0, 1);
				return 1;
			} else if(want == SSL_ERROR_SYSCALL) {
				if(errno != 0)
					log_err("SSL_read syscall: %s",
						strerror(errno));
				return 0;
			}
			log_crypto_err("could not SSL_read");
			return 0;
		}
		sldns_buffer_skip(c->buffer, (ssize_t)r);
	}
	if(sldns_buffer_remaining(c->buffer) <= 0) {
		tcp_callback_reader(c);
	}
	return 1;
#else
	(void)c;
	return 0;
#endif /* HAVE_SSL */
}

/** ssl write callback on TCP */
static int
ssl_handle_write(struct comm_point* c)
{
#ifdef HAVE_SSL
	int r;
	if(c->ssl_shake_state != comm_ssl_shake_none) {
		if(!ssl_handshake(c))
			return 0;
		if(c->ssl_shake_state != comm_ssl_shake_none)
			return 1;
	}
	/* ignore return, if fails we may simply block */
	(void)SSL_set_mode(c->ssl, SSL_MODE_ENABLE_PARTIAL_WRITE);
	if(c->tcp_byte_count < sizeof(uint16_t)) {
		uint16_t len = htons(sldns_buffer_limit(c->buffer));
		ERR_clear_error();
		if(sizeof(uint16_t)+sldns_buffer_remaining(c->buffer) <
			LDNS_RR_BUF_SIZE) {
			/* combine the tcp length and the query for write,
			 * this emulates writev */
			uint8_t buf[LDNS_RR_BUF_SIZE];
			memmove(buf, &len, sizeof(uint16_t));
			memmove(buf+sizeof(uint16_t),
				sldns_buffer_current(c->buffer),
				sldns_buffer_remaining(c->buffer));
			r = SSL_write(c->ssl, (void*)(buf+c->tcp_byte_count),
				(int)(sizeof(uint16_t)+
				sldns_buffer_remaining(c->buffer)
				- c->tcp_byte_count));
		} else {
			r = SSL_write(c->ssl,
				(void*)(((uint8_t*)&len)+c->tcp_byte_count),
				(int)(sizeof(uint16_t)-c->tcp_byte_count));
		}
		if(r <= 0) {
			int want = SSL_get_error(c->ssl, r);
			if(want == SSL_ERROR_ZERO_RETURN) {
				return 0; /* closed */
			} else if(want == SSL_ERROR_WANT_READ) {
				c->ssl_shake_state = comm_ssl_shake_read;
				comm_point_listen_for_rw(c, 1, 0);
				return 1; /* wait for read condition */
			} else if(want == SSL_ERROR_WANT_WRITE) {
				ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_WRITE);
				return 1; /* write more later */
			} else if(want == SSL_ERROR_SYSCALL) {
				if(errno != 0)
					log_err("SSL_write syscall: %s",
						strerror(errno));
				return 0;
			}
			log_crypto_err("could not SSL_write");
			return 0;
		}
		c->tcp_byte_count += r;
		if(c->tcp_byte_count < sizeof(uint16_t))
			return 1;
		sldns_buffer_set_position(c->buffer, c->tcp_byte_count -
			sizeof(uint16_t));
		if(sldns_buffer_remaining(c->buffer) == 0) {
			tcp_callback_writer(c);
			return 1;
		}
	}
	log_assert(sldns_buffer_remaining(c->buffer) > 0);
	ERR_clear_error();
	r = SSL_write(c->ssl, (void*)sldns_buffer_current(c->buffer),
		(int)sldns_buffer_remaining(c->buffer));
	if(r <= 0) {
		int want = SSL_get_error(c->ssl, r);
		if(want == SSL_ERROR_ZERO_RETURN) {
			return 0; /* closed */
		} else if(want == SSL_ERROR_WANT_READ) {
			c->ssl_shake_state = comm_ssl_shake_read;
			comm_point_listen_for_rw(c, 1, 0);
			return 1; /* wait for read condition */
		} else if(want == SSL_ERROR_WANT_WRITE) {
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_WRITE);
			return 1; /* write more later */
		} else if(want == SSL_ERROR_SYSCALL) {
			if(errno != 0)
				log_err("SSL_write syscall: %s",
					strerror(errno));
			return 0;
		}
		log_crypto_err("could not SSL_write");
		return 0;
	}
	sldns_buffer_skip(c->buffer, (ssize_t)r);

	if(sldns_buffer_remaining(c->buffer) == 0) {
		tcp_callback_writer(c);
	}
	return 1;
#else
	(void)c;
	return 0;
#endif /* HAVE_SSL */
}

/** handle ssl tcp connection with dns contents */
static int
ssl_handle_it(struct comm_point* c)
{
	if(c->tcp_is_reading)
		return ssl_handle_read(c);
	return ssl_handle_write(c);
}

/** Handle tcp reading callback. 
 * @param fd: file descriptor of socket.
 * @param c: comm point to read from into buffer.
 * @param short_ok: if true, very short packets are OK (for comm_local).
 * @return: 0 on error 
 */
static int
comm_point_tcp_handle_read(int fd, struct comm_point* c, int short_ok)
{
	ssize_t r;
	log_assert(c->type == comm_tcp || c->type == comm_local);
	if(c->ssl)
		return ssl_handle_it(c);
	if(!c->tcp_is_reading)
		return 0;

	log_assert(fd != -1);
	if(c->tcp_byte_count < sizeof(uint16_t)) {
		/* read length bytes */
		r = recv(fd,(void*)sldns_buffer_at(c->buffer,c->tcp_byte_count),
			sizeof(uint16_t)-c->tcp_byte_count, 0);
		if(r == 0)
			return 0;
		else if(r == -1) {
#ifndef USE_WINSOCK
			if(errno == EINTR || errno == EAGAIN)
				return 1;
#ifdef ECONNRESET
			if(errno == ECONNRESET && verbosity < 2)
				return 0; /* silence reset by peer */
#endif
			log_err_addr("read (in tcp s)", strerror(errno),
				&c->repinfo.addr, c->repinfo.addrlen);
#else /* USE_WINSOCK */
			if(WSAGetLastError() == WSAECONNRESET)
				return 0;
			if(WSAGetLastError() == WSAEINPROGRESS)
				return 1;
			if(WSAGetLastError() == WSAEWOULDBLOCK) {
				ub_winsock_tcp_wouldblock(c->ev->ev,
					UB_EV_READ);
				return 1;
			}
			log_err_addr("read (in tcp s)", 
				wsa_strerror(WSAGetLastError()),
				&c->repinfo.addr, c->repinfo.addrlen);
#endif
			return 0;
		} 
		c->tcp_byte_count += r;
		if(c->tcp_byte_count != sizeof(uint16_t))
			return 1;
		if(sldns_buffer_read_u16_at(c->buffer, 0) >
			sldns_buffer_capacity(c->buffer)) {
			verbose(VERB_QUERY, "tcp: dropped larger than buffer");
			return 0;
		}
		sldns_buffer_set_limit(c->buffer, 
			sldns_buffer_read_u16_at(c->buffer, 0));
		if(!short_ok && 
			sldns_buffer_limit(c->buffer) < LDNS_HEADER_SIZE) {
			verbose(VERB_QUERY, "tcp: dropped bogus too short.");
			return 0;
		}
		verbose(VERB_ALGO, "Reading tcp query of length %d", 
			(int)sldns_buffer_limit(c->buffer));
	}

	log_assert(sldns_buffer_remaining(c->buffer) > 0);
	r = recv(fd, (void*)sldns_buffer_current(c->buffer), 
		sldns_buffer_remaining(c->buffer), 0);
	if(r == 0) {
		return 0;
	} else if(r == -1) {
#ifndef USE_WINSOCK
		if(errno == EINTR || errno == EAGAIN)
			return 1;
		log_err_addr("read (in tcp r)", strerror(errno),
			&c->repinfo.addr, c->repinfo.addrlen);
#else /* USE_WINSOCK */
		if(WSAGetLastError() == WSAECONNRESET)
			return 0;
		if(WSAGetLastError() == WSAEINPROGRESS)
			return 1;
		if(WSAGetLastError() == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_READ);
			return 1;
		}
		log_err_addr("read (in tcp r)",
			wsa_strerror(WSAGetLastError()),
			&c->repinfo.addr, c->repinfo.addrlen);
#endif
		return 0;
	}
	sldns_buffer_skip(c->buffer, r);
	if(sldns_buffer_remaining(c->buffer) <= 0) {
		tcp_callback_reader(c);
	}
	return 1;
}

/** 
 * Handle tcp writing callback. 
 * @param fd: file descriptor of socket.
 * @param c: comm point to write buffer out of.
 * @return: 0 on error
 */
static int
comm_point_tcp_handle_write(int fd, struct comm_point* c)
{
	ssize_t r;
	struct sldns_buffer *buffer;
	log_assert(c->type == comm_tcp);
#ifdef USE_DNSCRYPT
	buffer = c->dnscrypt_buffer;
#else
	buffer = c->buffer;
#endif
	if(c->tcp_is_reading && !c->ssl)
		return 0;
	log_assert(fd != -1);
	if(c->tcp_byte_count == 0 && c->tcp_check_nb_connect) {
		/* check for pending error from nonblocking connect */
		/* from Stevens, unix network programming, vol1, 3rd ed, p450*/
		int error = 0;
		socklen_t len = (socklen_t)sizeof(error);
		if(getsockopt(fd, SOL_SOCKET, SO_ERROR, (void*)&error, 
			&len) < 0){
#ifndef USE_WINSOCK
			error = errno; /* on solaris errno is error */
#else /* USE_WINSOCK */
			error = WSAGetLastError();
#endif
		}
#ifndef USE_WINSOCK
#if defined(EINPROGRESS) && defined(EWOULDBLOCK)
		if(error == EINPROGRESS || error == EWOULDBLOCK)
			return 1; /* try again later */
		else
#endif
		if(error != 0 && verbosity < 2)
			return 0; /* silence lots of chatter in the logs */
                else if(error != 0) {
			log_err_addr("tcp connect", strerror(error),
				&c->repinfo.addr, c->repinfo.addrlen);
#else /* USE_WINSOCK */
		/* examine error */
		if(error == WSAEINPROGRESS)
			return 1;
		else if(error == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_WRITE);
			return 1;
		} else if(error != 0 && verbosity < 2)
			return 0;
		else if(error != 0) {
			log_err_addr("tcp connect", wsa_strerror(error),
				&c->repinfo.addr, c->repinfo.addrlen);
#endif /* USE_WINSOCK */
			return 0;
		}
	}
	if(c->ssl)
		return ssl_handle_it(c);

#ifdef USE_MSG_FASTOPEN
	/* Only try this on first use of a connection that uses tfo, 
	   otherwise fall through to normal write */
	/* Also, TFO support on WINDOWS not implemented at the moment */
	if(c->tcp_do_fastopen == 1) {
		/* this form of sendmsg() does both a connect() and send() so need to
		   look for various flavours of error*/
		uint16_t len = htons(sldns_buffer_limit(buffer));
		struct msghdr msg;
		struct iovec iov[2];
		c->tcp_do_fastopen = 0;
		memset(&msg, 0, sizeof(msg));
		iov[0].iov_base = (uint8_t*)&len + c->tcp_byte_count;
		iov[0].iov_len = sizeof(uint16_t) - c->tcp_byte_count;
		iov[1].iov_base = sldns_buffer_begin(buffer);
		iov[1].iov_len = sldns_buffer_limit(buffer);
		log_assert(iov[0].iov_len > 0);
		log_assert(iov[1].iov_len > 0);
		msg.msg_name = &c->repinfo.addr;
		msg.msg_namelen = c->repinfo.addrlen;
		msg.msg_iov = iov;
		msg.msg_iovlen = 2;
		r = sendmsg(fd, &msg, MSG_FASTOPEN);
		if (r == -1) {
#if defined(EINPROGRESS) && defined(EWOULDBLOCK)
			/* Handshake is underway, maybe because no TFO cookie available.
			   Come back to write the message*/
			if(errno == EINPROGRESS || errno == EWOULDBLOCK)
				return 1;
#endif
			if(errno == EINTR || errno == EAGAIN)
				return 1;
			/* Not handling EISCONN here as shouldn't ever hit that case.*/
			if(errno != EPIPE && errno != 0 && verbosity < 2)
				return 0; /* silence lots of chatter in the logs */
			if(errno != EPIPE && errno != 0) {
				log_err_addr("tcp sendmsg", strerror(errno),
					&c->repinfo.addr, c->repinfo.addrlen);
				return 0;
			}
			/* fallthrough to nonFASTOPEN
			 * (MSG_FASTOPEN on Linux 3 produces EPIPE)
			 * we need to perform connect() */
			if(connect(fd, (struct sockaddr *)&c->repinfo.addr, c->repinfo.addrlen) == -1) {
#ifdef EINPROGRESS
				if(errno == EINPROGRESS)
					return 1; /* wait until connect done*/
#endif
#ifdef USE_WINSOCK
				if(WSAGetLastError() == WSAEINPROGRESS ||
					WSAGetLastError() == WSAEWOULDBLOCK)
					return 1; /* wait until connect done*/
#endif
				if(tcp_connect_errno_needs_log(
					(struct sockaddr *)&c->repinfo.addr, c->repinfo.addrlen)) {
					log_err_addr("outgoing tcp: connect after EPIPE for fastopen",
						strerror(errno), &c->repinfo.addr, c->repinfo.addrlen);
				}
				return 0;
			}

		} else {
			c->tcp_byte_count += r;
			if(c->tcp_byte_count < sizeof(uint16_t))
				return 1;
			sldns_buffer_set_position(buffer, c->tcp_byte_count - 
				sizeof(uint16_t));
			if(sldns_buffer_remaining(buffer) == 0) {
				tcp_callback_writer(c);
				return 1;
			}
		}
	}
#endif /* USE_MSG_FASTOPEN */

	if(c->tcp_byte_count < sizeof(uint16_t)) {
		uint16_t len = htons(sldns_buffer_limit(buffer));
#ifdef HAVE_WRITEV
		struct iovec iov[2];
		iov[0].iov_base = (uint8_t*)&len + c->tcp_byte_count;
		iov[0].iov_len = sizeof(uint16_t) - c->tcp_byte_count;
		iov[1].iov_base = sldns_buffer_begin(buffer);
		iov[1].iov_len = sldns_buffer_limit(buffer);
		log_assert(iov[0].iov_len > 0);
		log_assert(iov[1].iov_len > 0);
		r = writev(fd, iov, 2);
#else /* HAVE_WRITEV */
		r = send(fd, (void*)(((uint8_t*)&len)+c->tcp_byte_count),
			sizeof(uint16_t)-c->tcp_byte_count, 0);
#endif /* HAVE_WRITEV */
		if(r == -1) {
#ifndef USE_WINSOCK
#  ifdef EPIPE
                	if(errno == EPIPE && verbosity < 2)
                        	return 0; /* silence 'broken pipe' */
  #endif
			if(errno == EINTR || errno == EAGAIN)
				return 1;
#  ifdef HAVE_WRITEV
			log_err_addr("tcp writev", strerror(errno),
				&c->repinfo.addr, c->repinfo.addrlen);
#  else /* HAVE_WRITEV */
			log_err_addr("tcp send s", strerror(errno),
				&c->repinfo.addr, c->repinfo.addrlen);
#  endif /* HAVE_WRITEV */
#else
			if(WSAGetLastError() == WSAENOTCONN)
				return 1;
			if(WSAGetLastError() == WSAEINPROGRESS)
				return 1;
			if(WSAGetLastError() == WSAEWOULDBLOCK) {
				ub_winsock_tcp_wouldblock(c->ev->ev,
					UB_EV_WRITE);
				return 1; 
			}
			log_err_addr("tcp send s",
				wsa_strerror(WSAGetLastError()),
				&c->repinfo.addr, c->repinfo.addrlen);
#endif
			return 0;
		}
		c->tcp_byte_count += r;
		if(c->tcp_byte_count < sizeof(uint16_t))
			return 1;
		sldns_buffer_set_position(buffer, c->tcp_byte_count - 
			sizeof(uint16_t));
		if(sldns_buffer_remaining(buffer) == 0) {
			tcp_callback_writer(c);
			return 1;
		}
	}
	log_assert(sldns_buffer_remaining(buffer) > 0);
	r = send(fd, (void*)sldns_buffer_current(buffer), 
		sldns_buffer_remaining(buffer), 0);
	if(r == -1) {
#ifndef USE_WINSOCK
		if(errno == EINTR || errno == EAGAIN)
			return 1;
		log_err_addr("tcp send r", strerror(errno),
			&c->repinfo.addr, c->repinfo.addrlen);
#else
		if(WSAGetLastError() == WSAEINPROGRESS)
			return 1;
		if(WSAGetLastError() == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_WRITE);
			return 1; 
		}
		log_err_addr("tcp send r", wsa_strerror(WSAGetLastError()),
			&c->repinfo.addr, c->repinfo.addrlen);
#endif
		return 0;
	}
	sldns_buffer_skip(buffer, r);

	if(sldns_buffer_remaining(buffer) == 0) {
		tcp_callback_writer(c);
	}
	
	return 1;
}

void 
comm_point_tcp_handle_callback(int fd, short event, void* arg)
{
	struct comm_point* c = (struct comm_point*)arg;
	log_assert(c->type == comm_tcp);
	ub_comm_base_now(c->ev->base);

#ifdef USE_DNSCRYPT
	/* Initialize if this is a dnscrypt socket */
	if(c->tcp_parent) {
		c->dnscrypt = c->tcp_parent->dnscrypt;
	}
	if(c->dnscrypt && c->dnscrypt_buffer == c->buffer) {
		c->dnscrypt_buffer = sldns_buffer_new(sldns_buffer_capacity(c->buffer));
		if(!c->dnscrypt_buffer) {
			log_err("Could not allocate dnscrypt buffer");
			reclaim_tcp_handler(c);
			if(!c->tcp_do_close) {
				fptr_ok(fptr_whitelist_comm_point(
					c->callback));
				(void)(*c->callback)(c, c->cb_arg, 
					NETEVENT_CLOSED, NULL);
			}
			return;
		}
	}
#endif

	if(event&UB_EV_READ) {
		if(!comm_point_tcp_handle_read(fd, c, 0)) {
			reclaim_tcp_handler(c);
			if(!c->tcp_do_close) {
				fptr_ok(fptr_whitelist_comm_point(
					c->callback));
				(void)(*c->callback)(c, c->cb_arg, 
					NETEVENT_CLOSED, NULL);
			}
		}
		return;
	}
	if(event&UB_EV_WRITE) {
		if(!comm_point_tcp_handle_write(fd, c)) {
			reclaim_tcp_handler(c);
			if(!c->tcp_do_close) {
				fptr_ok(fptr_whitelist_comm_point(
					c->callback));
				(void)(*c->callback)(c, c->cb_arg, 
					NETEVENT_CLOSED, NULL);
			}
		}
		return;
	}
	if(event&UB_EV_TIMEOUT) {
		verbose(VERB_QUERY, "tcp took too long, dropped");
		reclaim_tcp_handler(c);
		if(!c->tcp_do_close) {
			fptr_ok(fptr_whitelist_comm_point(c->callback));
			(void)(*c->callback)(c, c->cb_arg,
				NETEVENT_TIMEOUT, NULL);
		}
		return;
	}
	log_err("Ignored event %d for tcphdl.", event);
}

/** Make http handler free for next assignment */
static void
reclaim_http_handler(struct comm_point* c)
{
	log_assert(c->type == comm_http);
	if(c->ssl) {
#ifdef HAVE_SSL
		SSL_shutdown(c->ssl);
		SSL_free(c->ssl);
		c->ssl = NULL;
#endif
	}
	comm_point_close(c);
	if(c->tcp_parent) {
		c->tcp_parent->cur_tcp_count--;
		c->tcp_free = c->tcp_parent->tcp_free;
		c->tcp_parent->tcp_free = c;
		if(!c->tcp_free) {
			/* re-enable listening on accept socket */
			comm_point_start_listening(c->tcp_parent, -1, -1);
		}
	}
}

/** read more data for http (with ssl) */
static int
ssl_http_read_more(struct comm_point* c)
{
#ifdef HAVE_SSL
	int r;
	log_assert(sldns_buffer_remaining(c->buffer) > 0);
	ERR_clear_error();
	r = SSL_read(c->ssl, (void*)sldns_buffer_current(c->buffer),
		(int)sldns_buffer_remaining(c->buffer));
	if(r <= 0) {
		int want = SSL_get_error(c->ssl, r);
		if(want == SSL_ERROR_ZERO_RETURN) {
			return 0; /* shutdown, closed */
		} else if(want == SSL_ERROR_WANT_READ) {
			return 1; /* read more later */
		} else if(want == SSL_ERROR_WANT_WRITE) {
			c->ssl_shake_state = comm_ssl_shake_hs_write;
			comm_point_listen_for_rw(c, 0, 1);
			return 1;
		} else if(want == SSL_ERROR_SYSCALL) {
			if(errno != 0)
				log_err("SSL_read syscall: %s",
					strerror(errno));
			return 0;
		}
		log_crypto_err("could not SSL_read");
		return 0;
	}
	sldns_buffer_skip(c->buffer, (ssize_t)r);
	return 1;
#else
	(void)c;
	return 0;
#endif /* HAVE_SSL */
}

/** read more data for http */
static int
http_read_more(int fd, struct comm_point* c)
{
	ssize_t r;
	log_assert(sldns_buffer_remaining(c->buffer) > 0);
	r = recv(fd, (void*)sldns_buffer_current(c->buffer), 
		sldns_buffer_remaining(c->buffer), 0);
	if(r == 0) {
		return 0;
	} else if(r == -1) {
#ifndef USE_WINSOCK
		if(errno == EINTR || errno == EAGAIN)
			return 1;
		log_err_addr("read (in http r)", strerror(errno),
			&c->repinfo.addr, c->repinfo.addrlen);
#else /* USE_WINSOCK */
		if(WSAGetLastError() == WSAECONNRESET)
			return 0;
		if(WSAGetLastError() == WSAEINPROGRESS)
			return 1;
		if(WSAGetLastError() == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_READ);
			return 1;
		}
		log_err_addr("read (in http r)",
			wsa_strerror(WSAGetLastError()),
			&c->repinfo.addr, c->repinfo.addrlen);
#endif
		return 0;
	}
	sldns_buffer_skip(c->buffer, r);
	return 1;
}

/** return true if http header has been read (one line complete) */
static int
http_header_done(sldns_buffer* buf)
{
	size_t i;
	for(i=sldns_buffer_position(buf); i<sldns_buffer_limit(buf); i++) {
		/* there was a \r before the \n, but we ignore that */
		if((char)sldns_buffer_read_u8_at(buf, i) == '\n')
			return 1;
	}
	return 0;
}

/** return character string into buffer for header line, moves buffer
 * past that line and puts zero terminator into linefeed-newline */
static char*
http_header_line(sldns_buffer* buf)
{
	char* result = (char*)sldns_buffer_current(buf);
	size_t i;
	for(i=sldns_buffer_position(buf); i<sldns_buffer_limit(buf); i++) {
		/* terminate the string on the \r */
		if((char)sldns_buffer_read_u8_at(buf, i) == '\r')
			sldns_buffer_write_u8_at(buf, i, 0);
		/* terminate on the \n and skip past the it and done */
		if((char)sldns_buffer_read_u8_at(buf, i) == '\n') {
			sldns_buffer_write_u8_at(buf, i, 0);
			sldns_buffer_set_position(buf, i+1);
			return result;
		}
	}
	return NULL;
}

/** move unread buffer to start and clear rest for putting the rest into it */
static void
http_moveover_buffer(sldns_buffer* buf)
{
	size_t pos = sldns_buffer_position(buf);
	size_t len = sldns_buffer_remaining(buf);
	sldns_buffer_clear(buf);
	memmove(sldns_buffer_begin(buf), sldns_buffer_at(buf, pos), len);
	sldns_buffer_set_position(buf, len);
}

/** a http header is complete, process it */
static int
http_process_initial_header(struct comm_point* c)
{
	char* line = http_header_line(c->buffer);
	if(!line) return 1;
	verbose(VERB_ALGO, "http header: %s", line);
	if(strncasecmp(line, "HTTP/1.1 ", 9) == 0) {
		/* check returncode */
		if(line[9] != '2') {
			verbose(VERB_ALGO, "http bad status %s", line+9);
			return 0;
		}
	} else if(strncasecmp(line, "Content-Length: ", 16) == 0) {
		if(!c->http_is_chunked)
			c->tcp_byte_count = (size_t)atoi(line+16);
	} else if(strncasecmp(line, "Transfer-Encoding: chunked", 19+7) == 0) {
		c->tcp_byte_count = 0;
		c->http_is_chunked = 1;
	} else if(line[0] == 0) {
		/* end of initial headers */
		c->http_in_headers = 0;
		if(c->http_is_chunked)
			c->http_in_chunk_headers = 1;
		/* remove header text from front of buffer
		 * the buffer is going to be used to return the data segment
		 * itself and we don't want the header to get returned
		 * prepended with it */
		http_moveover_buffer(c->buffer);
		sldns_buffer_flip(c->buffer);
		return 1;
	}
	/* ignore other headers */
	return 1;
}

/** a chunk header is complete, process it, return 0=fail, 1=continue next
 * header line, 2=done with chunked transfer*/
static int
http_process_chunk_header(struct comm_point* c)
{
	char* line = http_header_line(c->buffer);
	if(!line) return 1;
	if(c->http_in_chunk_headers == 3) {
		verbose(VERB_ALGO, "http chunk trailer: %s", line);
		/* are we done ? */
		if(line[0] == 0 && c->tcp_byte_count == 0) {
			/* callback of http reader when NETEVENT_DONE,
			 * end of data, with no data in buffer */
			sldns_buffer_set_position(c->buffer, 0);
			sldns_buffer_set_limit(c->buffer, 0);
			fptr_ok(fptr_whitelist_comm_point(c->callback));
			(void)(*c->callback)(c, c->cb_arg, NETEVENT_DONE, NULL);
			/* return that we are done */
			return 2;
		}
		if(line[0] == 0) {
			/* continue with header of the next chunk */
			c->http_in_chunk_headers = 1;
			/* remove header text from front of buffer */
			http_moveover_buffer(c->buffer);
			sldns_buffer_flip(c->buffer);
			return 1;
		}
		/* ignore further trail headers */
		return 1;
	}
	verbose(VERB_ALGO, "http chunk header: %s", line);
	if(c->http_in_chunk_headers == 1) {
		/* read chunked start line */
		char* end = NULL;
		c->tcp_byte_count = (size_t)strtol(line, &end, 16);
		if(end == line)
			return 0;
		c->http_in_chunk_headers = 0;
		/* remove header text from front of buffer */
		http_moveover_buffer(c->buffer);
		sldns_buffer_flip(c->buffer);
		if(c->tcp_byte_count == 0) {
			/* done with chunks, process chunk_trailer lines */
			c->http_in_chunk_headers = 3;
		}
		return 1;
	}
	/* ignore other headers */
	return 1;
}

/** handle nonchunked data segment */
static int
http_nonchunk_segment(struct comm_point* c)
{
	/* c->buffer at position..limit has new data we read in.
	 * the buffer itself is full of nonchunked data.
	 * we are looking to read tcp_byte_count more data
	 * and then the transfer is done. */
	size_t remainbufferlen;
	size_t got_now = sldns_buffer_limit(c->buffer) - c->http_stored;
	if(c->tcp_byte_count <= got_now) {
		/* done, this is the last data fragment */
		c->http_stored = 0;
		sldns_buffer_set_position(c->buffer, 0);
		fptr_ok(fptr_whitelist_comm_point(c->callback));
		(void)(*c->callback)(c, c->cb_arg, NETEVENT_DONE, NULL);
		return 1;
	}
	c->tcp_byte_count -= got_now;
	/* if we have the buffer space,
	 * read more data collected into the buffer */
	remainbufferlen = sldns_buffer_capacity(c->buffer) -
		sldns_buffer_limit(c->buffer);
	if(remainbufferlen >= c->tcp_byte_count ||
		remainbufferlen >= 2048) {
		size_t total = sldns_buffer_limit(c->buffer);
		sldns_buffer_clear(c->buffer);
		sldns_buffer_set_position(c->buffer, total);
		c->http_stored = total;
		/* return and wait to read more */
		return 1;
	}
	/* call callback with this data amount, then
	 * wait for more */
	c->http_stored = 0;
	sldns_buffer_set_position(c->buffer, 0);
	fptr_ok(fptr_whitelist_comm_point(c->callback));
	(void)(*c->callback)(c, c->cb_arg, NETEVENT_NOERROR, NULL);
	/* c->callback has to buffer_clear(c->buffer). */
	/* return and wait to read more */
	return 1;
}

/** handle nonchunked data segment, return 0=fail, 1=wait, 2=process more */
static int
http_chunked_segment(struct comm_point* c)
{
	/* the c->buffer has from position..limit new data we read. */
	/* the current chunk has length tcp_byte_count.
	 * once we read that read more chunk headers.
	 */
	size_t remainbufferlen;
	size_t got_now = sldns_buffer_limit(c->buffer) - c->http_stored;
	if(c->tcp_byte_count <= got_now) {
		/* the chunk has completed (with perhaps some extra data
		 * from next chunk header and next chunk) */
		/* save too much info into temp buffer */
		size_t fraglen;
		struct comm_reply repinfo;
		c->http_stored = 0;
		sldns_buffer_skip(c->buffer, (ssize_t)c->tcp_byte_count);
		sldns_buffer_clear(c->http_temp);
		sldns_buffer_write(c->http_temp,
			sldns_buffer_current(c->buffer),
			sldns_buffer_remaining(c->buffer));
		sldns_buffer_flip(c->http_temp);

		/* callback with this fragment */
		fraglen = sldns_buffer_position(c->buffer);
		sldns_buffer_set_position(c->buffer, 0);
		sldns_buffer_set_limit(c->buffer, fraglen);
		repinfo = c->repinfo;
		fptr_ok(fptr_whitelist_comm_point(c->callback));
		(void)(*c->callback)(c, c->cb_arg, NETEVENT_NOERROR, &repinfo);
		/* c->callback has to buffer_clear(). */

		/* is commpoint deleted? */
		if(!repinfo.c) {
			return 1;
		}
		/* copy waiting info */
		sldns_buffer_clear(c->buffer);
		sldns_buffer_write(c->buffer,
			sldns_buffer_begin(c->http_temp),
			sldns_buffer_remaining(c->http_temp));
		sldns_buffer_flip(c->buffer);
		/* process end of chunk trailer header lines, until
		 * an empty line */
		c->http_in_chunk_headers = 3;
		/* process more data in buffer (if any) */
		return 2;
	}
	c->tcp_byte_count -= got_now;

	/* if we have the buffer space,
	 * read more data collected into the buffer */
	remainbufferlen = sldns_buffer_capacity(c->buffer) -
		sldns_buffer_limit(c->buffer);
	if(remainbufferlen >= c->tcp_byte_count ||
		remainbufferlen >= 2048) {
		size_t total = sldns_buffer_limit(c->buffer);
		sldns_buffer_clear(c->buffer);
		sldns_buffer_set_position(c->buffer, total);
		c->http_stored = total;
		/* return and wait to read more */
		return 1;
	}
	
	/* callback of http reader for a new part of the data */
	c->http_stored = 0;
	sldns_buffer_set_position(c->buffer, 0);
	fptr_ok(fptr_whitelist_comm_point(c->callback));
	(void)(*c->callback)(c, c->cb_arg, NETEVENT_NOERROR, NULL);
	/* c->callback has to buffer_clear(c->buffer). */
	/* return and wait to read more */
	return 1;
}

/**
 * Handle http reading callback. 
 * @param fd: file descriptor of socket.
 * @param c: comm point to read from into buffer.
 * @return: 0 on error 
 */
static int
comm_point_http_handle_read(int fd, struct comm_point* c)
{
	log_assert(c->type == comm_http);
	log_assert(fd != -1);

	/* if we are in ssl handshake, handle SSL handshake */
#ifdef HAVE_SSL
	if(c->ssl && c->ssl_shake_state != comm_ssl_shake_none) {
		if(!ssl_handshake(c))
			return 0;
		if(c->ssl_shake_state != comm_ssl_shake_none)
			return 1;
	}
#endif /* HAVE_SSL */

	if(!c->tcp_is_reading)
		return 1;
	/* read more data */
	if(c->ssl) {
		if(!ssl_http_read_more(c))
			return 0;
	} else {
		if(!http_read_more(fd, c))
			return 0;
	}

	sldns_buffer_flip(c->buffer);
	while(sldns_buffer_remaining(c->buffer) > 0) {
		/* if we are reading headers, read more headers */
		if(c->http_in_headers || c->http_in_chunk_headers) {
			/* if header is done, process the header */
			if(!http_header_done(c->buffer)) {
				/* copy remaining data to front of buffer
				 * and set rest for writing into it */
				http_moveover_buffer(c->buffer);
				/* return and wait to read more */
				return 1;
			}
			if(!c->http_in_chunk_headers) {
				/* process initial headers */
				if(!http_process_initial_header(c))
					return 0;
			} else {
				/* process chunk headers */
				int r = http_process_chunk_header(c);
				if(r == 0) return 0;
				if(r == 2) return 1; /* done */
				/* r == 1, continue */
			}
			/* see if we have more to process */
			continue;
		}

		if(!c->http_is_chunked) {
			/* if we are reading nonchunks, process that*/
			return http_nonchunk_segment(c);
		} else {
			/* if we are reading chunks, read the chunk */
			int r = http_chunked_segment(c);
			if(r == 0) return 0;
			if(r == 1) return 1;
			continue;
		}
	}
	/* broke out of the loop; could not process header instead need
	 * to read more */
	/* moveover any remaining data and read more data */
	http_moveover_buffer(c->buffer);
	/* return and wait to read more */
	return 1;
}

/** check pending connect for http */
static int
http_check_connect(int fd, struct comm_point* c)
{
	/* check for pending error from nonblocking connect */
	/* from Stevens, unix network programming, vol1, 3rd ed, p450*/
	int error = 0;
	socklen_t len = (socklen_t)sizeof(error);
	if(getsockopt(fd, SOL_SOCKET, SO_ERROR, (void*)&error, 
		&len) < 0){
#ifndef USE_WINSOCK
		error = errno; /* on solaris errno is error */
#else /* USE_WINSOCK */
		error = WSAGetLastError();
#endif
	}
#ifndef USE_WINSOCK
#if defined(EINPROGRESS) && defined(EWOULDBLOCK)
	if(error == EINPROGRESS || error == EWOULDBLOCK)
		return 1; /* try again later */
	else
#endif
	if(error != 0 && verbosity < 2)
		return 0; /* silence lots of chatter in the logs */
	else if(error != 0) {
		log_err_addr("http connect", strerror(error),
			&c->repinfo.addr, c->repinfo.addrlen);
#else /* USE_WINSOCK */
	/* examine error */
	if(error == WSAEINPROGRESS)
		return 1;
	else if(error == WSAEWOULDBLOCK) {
		ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_WRITE);
		return 1;
	} else if(error != 0 && verbosity < 2)
		return 0;
	else if(error != 0) {
		log_err_addr("http connect", wsa_strerror(error),
			&c->repinfo.addr, c->repinfo.addrlen);
#endif /* USE_WINSOCK */
		return 0;
	}
	/* keep on processing this socket */
	return 2;
}

/** write more data for http (with ssl) */
static int
ssl_http_write_more(struct comm_point* c)
{
#ifdef HAVE_SSL
	int r;
	log_assert(sldns_buffer_remaining(c->buffer) > 0);
	ERR_clear_error();
	r = SSL_write(c->ssl, (void*)sldns_buffer_current(c->buffer),
		(int)sldns_buffer_remaining(c->buffer));
	if(r <= 0) {
		int want = SSL_get_error(c->ssl, r);
		if(want == SSL_ERROR_ZERO_RETURN) {
			return 0; /* closed */
		} else if(want == SSL_ERROR_WANT_READ) {
			c->ssl_shake_state = comm_ssl_shake_read;
			comm_point_listen_for_rw(c, 1, 0);
			return 1; /* wait for read condition */
		} else if(want == SSL_ERROR_WANT_WRITE) {
			return 1; /* write more later */
		} else if(want == SSL_ERROR_SYSCALL) {
			if(errno != 0)
				log_err("SSL_write syscall: %s",
					strerror(errno));
			return 0;
		}
		log_crypto_err("could not SSL_write");
		return 0;
	}
	sldns_buffer_skip(c->buffer, (ssize_t)r);
	return 1;
#else
	(void)c;
	return 0;
#endif /* HAVE_SSL */
}

/** write more data for http */
static int
http_write_more(int fd, struct comm_point* c)
{
	ssize_t r;
	log_assert(sldns_buffer_remaining(c->buffer) > 0);
	r = send(fd, (void*)sldns_buffer_current(c->buffer), 
		sldns_buffer_remaining(c->buffer), 0);
	if(r == -1) {
#ifndef USE_WINSOCK
		if(errno == EINTR || errno == EAGAIN)
			return 1;
		log_err_addr("http send r", strerror(errno),
			&c->repinfo.addr, c->repinfo.addrlen);
#else
		if(WSAGetLastError() == WSAEINPROGRESS)
			return 1;
		if(WSAGetLastError() == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_WRITE);
			return 1; 
		}
		log_err_addr("http send r", wsa_strerror(WSAGetLastError()),
			&c->repinfo.addr, c->repinfo.addrlen);
#endif
		return 0;
	}
	sldns_buffer_skip(c->buffer, r);
	return 1;
}

/** 
 * Handle http writing callback. 
 * @param fd: file descriptor of socket.
 * @param c: comm point to write buffer out of.
 * @return: 0 on error
 */
static int
comm_point_http_handle_write(int fd, struct comm_point* c)
{
	log_assert(c->type == comm_http);
	log_assert(fd != -1);

	/* check pending connect errors, if that fails, we wait for more,
	 * or we can continue to write contents */
	if(c->tcp_check_nb_connect) {
		int r = http_check_connect(fd, c);
		if(r == 0) return 0;
		if(r == 1) return 1;
		c->tcp_check_nb_connect = 0;
	}
	/* if we are in ssl handshake, handle SSL handshake */
#ifdef HAVE_SSL
	if(c->ssl && c->ssl_shake_state != comm_ssl_shake_none) {
		if(!ssl_handshake(c))
			return 0;
		if(c->ssl_shake_state != comm_ssl_shake_none)
			return 1;
	}
#endif /* HAVE_SSL */
	if(c->tcp_is_reading)
		return 1;
	/* if we are writing, write more */
	if(c->ssl) {
		if(!ssl_http_write_more(c))
			return 0;
	} else {
		if(!http_write_more(fd, c))
			return 0;
	}

	/* we write a single buffer contents, that can contain
	 * the http request, and then flip to read the results */
	/* see if write is done */
	if(sldns_buffer_remaining(c->buffer) == 0) {
		sldns_buffer_clear(c->buffer);
		if(c->tcp_do_toggle_rw)
			c->tcp_is_reading = 1;
		c->tcp_byte_count = 0;
		/* switch from listening(write) to listening(read) */
		comm_point_stop_listening(c);
		comm_point_start_listening(c, -1, -1);
	}
	return 1;
}

void 
comm_point_http_handle_callback(int fd, short event, void* arg)
{
	struct comm_point* c = (struct comm_point*)arg;
	log_assert(c->type == comm_http);
	ub_comm_base_now(c->ev->base);

	if(event&UB_EV_READ) {
		if(!comm_point_http_handle_read(fd, c)) {
			reclaim_http_handler(c);
			if(!c->tcp_do_close) {
				fptr_ok(fptr_whitelist_comm_point(
					c->callback));
				(void)(*c->callback)(c, c->cb_arg, 
					NETEVENT_CLOSED, NULL);
			}
		}
		return;
	}
	if(event&UB_EV_WRITE) {
		if(!comm_point_http_handle_write(fd, c)) {
			reclaim_http_handler(c);
			if(!c->tcp_do_close) {
				fptr_ok(fptr_whitelist_comm_point(
					c->callback));
				(void)(*c->callback)(c, c->cb_arg, 
					NETEVENT_CLOSED, NULL);
			}
		}
		return;
	}
	if(event&UB_EV_TIMEOUT) {
		verbose(VERB_QUERY, "http took too long, dropped");
		reclaim_http_handler(c);
		if(!c->tcp_do_close) {
			fptr_ok(fptr_whitelist_comm_point(c->callback));
			(void)(*c->callback)(c, c->cb_arg,
				NETEVENT_TIMEOUT, NULL);
		}
		return;
	}
	log_err("Ignored event %d for httphdl.", event);
}

void comm_point_local_handle_callback(int fd, short event, void* arg)
{
	struct comm_point* c = (struct comm_point*)arg;
	log_assert(c->type == comm_local);
	ub_comm_base_now(c->ev->base);

	if(event&UB_EV_READ) {
		if(!comm_point_tcp_handle_read(fd, c, 1)) {
			fptr_ok(fptr_whitelist_comm_point(c->callback));
			(void)(*c->callback)(c, c->cb_arg, NETEVENT_CLOSED, 
				NULL);
		}
		return;
	}
	log_err("Ignored event %d for localhdl.", event);
}

void comm_point_raw_handle_callback(int ATTR_UNUSED(fd), 
	short event, void* arg)
{
	struct comm_point* c = (struct comm_point*)arg;
	int err = NETEVENT_NOERROR;
	log_assert(c->type == comm_raw);
	ub_comm_base_now(c->ev->base);
	
	if(event&UB_EV_TIMEOUT)
		err = NETEVENT_TIMEOUT;
	fptr_ok(fptr_whitelist_comm_point_raw(c->callback));
	(void)(*c->callback)(c, c->cb_arg, err, NULL);
}

struct comm_point* 
comm_point_create_udp(struct comm_base *base, int fd, sldns_buffer* buffer,
	comm_point_callback_type* callback, void* callback_arg)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = fd;
	c->buffer = buffer;
	c->timeout = NULL;
	c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	c->tcp_parent = NULL;
	c->max_tcp_count = 0;
	c->cur_tcp_count = 0;
	c->tcp_handlers = NULL;
	c->tcp_free = NULL;
	c->type = comm_udp;
	c->tcp_do_close = 0;
	c->do_not_close = 0;
	c->tcp_do_toggle_rw = 0;
	c->tcp_check_nb_connect = 0;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 0;
#endif
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	c->dnscrypt_buffer = buffer;
#endif
	c->inuse = 0;
	c->callback = callback;
	c->cb_arg = callback_arg;
	evbits = UB_EV_READ | UB_EV_PERSIST;
	/* ub_event stuff */
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_udp_callback, c);
	if(c->ev->ev == NULL) {
		log_err("could not baseset udp event");
		comm_point_delete(c);
		return NULL;
	}
	if(fd!=-1 && ub_event_add(c->ev->ev, c->timeout) != 0 ) {
		log_err("could not add udp event");
		comm_point_delete(c);
		return NULL;
	}
	return c;
}

struct comm_point* 
comm_point_create_udp_ancil(struct comm_base *base, int fd, 
	sldns_buffer* buffer, 
	comm_point_callback_type* callback, void* callback_arg)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = fd;
	c->buffer = buffer;
	c->timeout = NULL;
	c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	c->tcp_parent = NULL;
	c->max_tcp_count = 0;
	c->cur_tcp_count = 0;
	c->tcp_handlers = NULL;
	c->tcp_free = NULL;
	c->type = comm_udp;
	c->tcp_do_close = 0;
	c->do_not_close = 0;
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	c->dnscrypt_buffer = buffer;
#endif
	c->inuse = 0;
	c->tcp_do_toggle_rw = 0;
	c->tcp_check_nb_connect = 0;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 0;
#endif
	c->callback = callback;
	c->cb_arg = callback_arg;
	evbits = UB_EV_READ | UB_EV_PERSIST;
	/* ub_event stuff */
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_udp_ancil_callback, c);
	if(c->ev->ev == NULL) {
		log_err("could not baseset udp event");
		comm_point_delete(c);
		return NULL;
	}
	if(fd!=-1 && ub_event_add(c->ev->ev, c->timeout) != 0 ) {
		log_err("could not add udp event");
		comm_point_delete(c);
		return NULL;
	}
	return c;
}

static struct comm_point* 
comm_point_create_tcp_handler(struct comm_base *base, 
	struct comm_point* parent, size_t bufsize,
        comm_point_callback_type* callback, void* callback_arg)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = -1;
	c->buffer = sldns_buffer_new(bufsize);
	if(!c->buffer) {
		free(c->ev);
		free(c);
		return NULL;
	}
	c->timeout = (struct timeval*)malloc(sizeof(struct timeval));
	if(!c->timeout) {
		sldns_buffer_free(c->buffer);
		free(c->ev);
		free(c);
		return NULL;
	}
	c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	c->tcp_parent = parent;
	c->tcp_timeout_msec = parent->tcp_timeout_msec;
	c->tcp_conn_limit = parent->tcp_conn_limit;
	c->tcl_addr = NULL;
	c->tcp_keepalive = 0;
	c->max_tcp_count = 0;
	c->cur_tcp_count = 0;
	c->tcp_handlers = NULL;
	c->tcp_free = NULL;
	c->type = comm_tcp;
	c->tcp_do_close = 0;
	c->do_not_close = 0;
	c->tcp_do_toggle_rw = 1;
	c->tcp_check_nb_connect = 0;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 0;
#endif
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	/* We don't know just yet if this is a dnscrypt channel. Allocation
	 * will be done when handling the callback. */
	c->dnscrypt_buffer = c->buffer;
#endif
	c->repinfo.c = c;
	c->callback = callback;
	c->cb_arg = callback_arg;
	/* add to parent free list */
	c->tcp_free = parent->tcp_free;
	parent->tcp_free = c;
	/* ub_event stuff */
	evbits = UB_EV_PERSIST | UB_EV_READ | UB_EV_TIMEOUT;
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_tcp_handle_callback, c);
	if(c->ev->ev == NULL)
	{
		log_err("could not basetset tcphdl event");
		parent->tcp_free = c->tcp_free;
		free(c->ev);
		free(c);
		return NULL;
	}
	return c;
}

struct comm_point* 
comm_point_create_tcp(struct comm_base *base, int fd, int num,
	int idle_timeout, struct tcl_list* tcp_conn_limit, size_t bufsize,
        comm_point_callback_type* callback, void* callback_arg)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	int i;
	/* first allocate the TCP accept listener */
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = fd;
	c->buffer = NULL;
	c->timeout = NULL;
	c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	c->tcp_timeout_msec = idle_timeout;
	c->tcp_conn_limit = tcp_conn_limit;
	c->tcl_addr = NULL;
	c->tcp_keepalive = 0;
	c->tcp_parent = NULL;
	c->max_tcp_count = num;
	c->cur_tcp_count = 0;
	c->tcp_handlers = (struct comm_point**)calloc((size_t)num,
		sizeof(struct comm_point*));
	if(!c->tcp_handlers) {
		free(c->ev);
		free(c);
		return NULL;
	}
	c->tcp_free = NULL;
	c->type = comm_tcp_accept;
	c->tcp_do_close = 0;
	c->do_not_close = 0;
	c->tcp_do_toggle_rw = 0;
	c->tcp_check_nb_connect = 0;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 0;
#endif
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	c->dnscrypt_buffer = NULL;
#endif
	c->callback = NULL;
	c->cb_arg = NULL;
	evbits = UB_EV_READ | UB_EV_PERSIST;
	/* ub_event stuff */
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_tcp_accept_callback, c);
	if(c->ev->ev == NULL) {
		log_err("could not baseset tcpacc event");
		comm_point_delete(c);
		return NULL;
	}
	if (ub_event_add(c->ev->ev, c->timeout) != 0) {
		log_err("could not add tcpacc event");
		comm_point_delete(c);
		return NULL;
	}
	/* now prealloc the tcp handlers */
	for(i=0; i<num; i++) {
		c->tcp_handlers[i] = comm_point_create_tcp_handler(base,
			c, bufsize, callback, callback_arg);
		if(!c->tcp_handlers[i]) {
			comm_point_delete(c);
			return NULL;
		}
	}
	
	return c;
}

struct comm_point* 
comm_point_create_tcp_out(struct comm_base *base, size_t bufsize,
        comm_point_callback_type* callback, void* callback_arg)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = -1;
	c->buffer = sldns_buffer_new(bufsize);
	if(!c->buffer) {
		free(c->ev);
		free(c);
		return NULL;
	}
	c->timeout = NULL;
	c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	c->tcp_timeout_msec = TCP_QUERY_TIMEOUT;
	c->tcp_conn_limit = NULL;
	c->tcl_addr = NULL;
	c->tcp_keepalive = 0;
	c->tcp_parent = NULL;
	c->max_tcp_count = 0;
	c->cur_tcp_count = 0;
	c->tcp_handlers = NULL;
	c->tcp_free = NULL;
	c->type = comm_tcp;
	c->tcp_do_close = 0;
	c->do_not_close = 0;
	c->tcp_do_toggle_rw = 1;
	c->tcp_check_nb_connect = 1;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 1;
#endif
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	c->dnscrypt_buffer = c->buffer;
#endif
	c->repinfo.c = c;
	c->callback = callback;
	c->cb_arg = callback_arg;
	evbits = UB_EV_PERSIST | UB_EV_WRITE;
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_tcp_handle_callback, c);
	if(c->ev->ev == NULL)
	{
		log_err("could not baseset tcpout event");
		sldns_buffer_free(c->buffer);
		free(c->ev);
		free(c);
		return NULL;
	}

	return c;
}

struct comm_point* 
comm_point_create_http_out(struct comm_base *base, size_t bufsize,
        comm_point_callback_type* callback, void* callback_arg,
	sldns_buffer* temp)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = -1;
	c->buffer = sldns_buffer_new(bufsize);
	if(!c->buffer) {
		free(c->ev);
		free(c);
		return NULL;
	}
	c->timeout = NULL;
	c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	c->tcp_parent = NULL;
	c->max_tcp_count = 0;
	c->cur_tcp_count = 0;
	c->tcp_handlers = NULL;
	c->tcp_free = NULL;
	c->type = comm_http;
	c->tcp_do_close = 0;
	c->do_not_close = 0;
	c->tcp_do_toggle_rw = 1;
	c->tcp_check_nb_connect = 1;
	c->http_in_headers = 1;
	c->http_in_chunk_headers = 0;
	c->http_is_chunked = 0;
	c->http_temp = temp;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 1;
#endif
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	c->dnscrypt_buffer = c->buffer;
#endif
	c->repinfo.c = c;
	c->callback = callback;
	c->cb_arg = callback_arg;
	evbits = UB_EV_PERSIST | UB_EV_WRITE;
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_http_handle_callback, c);
	if(c->ev->ev == NULL)
	{
		log_err("could not baseset tcpout event");
#ifdef HAVE_SSL
		SSL_free(c->ssl);
#endif
		sldns_buffer_free(c->buffer);
		free(c->ev);
		free(c);
		return NULL;
	}

	return c;
}

struct comm_point* 
comm_point_create_local(struct comm_base *base, int fd, size_t bufsize,
        comm_point_callback_type* callback, void* callback_arg)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = fd;
	c->buffer = sldns_buffer_new(bufsize);
	if(!c->buffer) {
		free(c->ev);
		free(c);
		return NULL;
	}
	c->timeout = NULL;
	c->tcp_is_reading = 1;
	c->tcp_byte_count = 0;
	c->tcp_parent = NULL;
	c->max_tcp_count = 0;
	c->cur_tcp_count = 0;
	c->tcp_handlers = NULL;
	c->tcp_free = NULL;
	c->type = comm_local;
	c->tcp_do_close = 0;
	c->do_not_close = 1;
	c->tcp_do_toggle_rw = 0;
	c->tcp_check_nb_connect = 0;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 0;
#endif
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	c->dnscrypt_buffer = c->buffer;
#endif
	c->callback = callback;
	c->cb_arg = callback_arg;
	/* ub_event stuff */
	evbits = UB_EV_PERSIST | UB_EV_READ;
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_local_handle_callback, c);
	if(c->ev->ev == NULL) {
		log_err("could not baseset localhdl event");
		free(c->ev);
		free(c);
		return NULL;
	}
	if (ub_event_add(c->ev->ev, c->timeout) != 0) {
		log_err("could not add localhdl event");
		ub_event_free(c->ev->ev);
		free(c->ev);
		free(c);
		return NULL;
	}
	return c;
}

struct comm_point* 
comm_point_create_raw(struct comm_base* base, int fd, int writing, 
	comm_point_callback_type* callback, void* callback_arg)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = fd;
	c->buffer = NULL;
	c->timeout = NULL;
	c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	c->tcp_parent = NULL;
	c->max_tcp_count = 0;
	c->cur_tcp_count = 0;
	c->tcp_handlers = NULL;
	c->tcp_free = NULL;
	c->type = comm_raw;
	c->tcp_do_close = 0;
	c->do_not_close = 1;
	c->tcp_do_toggle_rw = 0;
	c->tcp_check_nb_connect = 0;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 0;
#endif
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	c->dnscrypt_buffer = c->buffer;
#endif
	c->callback = callback;
	c->cb_arg = callback_arg;
	/* ub_event stuff */
	if(writing)
		evbits = UB_EV_PERSIST | UB_EV_WRITE;
	else 	evbits = UB_EV_PERSIST | UB_EV_READ;
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_raw_handle_callback, c);
	if(c->ev->ev == NULL) {
		log_err("could not baseset rawhdl event");
		free(c->ev);
		free(c);
		return NULL;
	}
	if (ub_event_add(c->ev->ev, c->timeout) != 0) {
		log_err("could not add rawhdl event");
		ub_event_free(c->ev->ev);
		free(c->ev);
		free(c);
		return NULL;
	}
	return c;
}

void 
comm_point_close(struct comm_point* c)
{
	if(!c)
		return;
	if(c->fd != -1) {
		if(ub_event_del(c->ev->ev) != 0) {
			log_err("could not event_del on close");
		}
	}
	tcl_close_connection(c->tcl_addr);
	/* close fd after removing from event lists, or epoll.. is messed up */
	if(c->fd != -1 && !c->do_not_close) {
		if(c->type == comm_tcp || c->type == comm_http) {
			/* delete sticky events for the fd, it gets closed */
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_READ);
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_WRITE);
		}
		verbose(VERB_ALGO, "close fd %d", c->fd);
#ifndef USE_WINSOCK
		close(c->fd);
#else
		closesocket(c->fd);
#endif
	}
	c->fd = -1;
}

void 
comm_point_delete(struct comm_point* c)
{
	if(!c) 
		return;
	if((c->type == comm_tcp || c->type == comm_http) && c->ssl) {
#ifdef HAVE_SSL
		SSL_shutdown(c->ssl);
		SSL_free(c->ssl);
#endif
	}
	comm_point_close(c);
	if(c->tcp_handlers) {
		int i;
		for(i=0; i<c->max_tcp_count; i++)
			comm_point_delete(c->tcp_handlers[i]);
		free(c->tcp_handlers);
	}
	free(c->timeout);
	if(c->type == comm_tcp || c->type == comm_local || c->type == comm_http) {
		sldns_buffer_free(c->buffer);
#ifdef USE_DNSCRYPT
		if(c->dnscrypt && c->dnscrypt_buffer != c->buffer) {
			sldns_buffer_free(c->dnscrypt_buffer);
		}
#endif
	}
	ub_event_free(c->ev->ev);
	free(c->ev);
	free(c);
}

void 
comm_point_send_reply(struct comm_reply *repinfo)
{
	struct sldns_buffer* buffer;
	log_assert(repinfo && repinfo->c);
#ifdef USE_DNSCRYPT
	buffer = repinfo->c->dnscrypt_buffer;
	if(!dnsc_handle_uncurved_request(repinfo)) {
		return;
	}
#else
	buffer = repinfo->c->buffer;
#endif
	if(repinfo->c->type == comm_udp) {
		if(repinfo->srctype)
			comm_point_send_udp_msg_if(repinfo->c, 
			buffer, (struct sockaddr*)&repinfo->addr, 
			repinfo->addrlen, repinfo);
		else
			comm_point_send_udp_msg(repinfo->c, buffer,
			(struct sockaddr*)&repinfo->addr, repinfo->addrlen);
#ifdef USE_DNSTAP
		if(repinfo->c->dtenv != NULL &&
		   repinfo->c->dtenv->log_client_response_messages)
			dt_msg_send_client_response(repinfo->c->dtenv,
			&repinfo->addr, repinfo->c->type, repinfo->c->buffer);
#endif
	} else {
#ifdef USE_DNSTAP
		if(repinfo->c->tcp_parent->dtenv != NULL &&
		   repinfo->c->tcp_parent->dtenv->log_client_response_messages)
			dt_msg_send_client_response(repinfo->c->tcp_parent->dtenv,
			&repinfo->addr, repinfo->c->type, repinfo->c->buffer);
#endif
		comm_point_start_listening(repinfo->c, -1,
			repinfo->c->tcp_timeout_msec);
	}
}

void 
comm_point_drop_reply(struct comm_reply* repinfo)
{
	if(!repinfo)
		return;
	log_assert(repinfo && repinfo->c);
	log_assert(repinfo->c->type != comm_tcp_accept);
	if(repinfo->c->type == comm_udp)
		return;
	reclaim_tcp_handler(repinfo->c);
}

void 
comm_point_stop_listening(struct comm_point* c)
{
	verbose(VERB_ALGO, "comm point stop listening %d", c->fd);
	if(ub_event_del(c->ev->ev) != 0) {
		log_err("event_del error to stoplisten");
	}
}

void 
comm_point_start_listening(struct comm_point* c, int newfd, int msec)
{
	verbose(VERB_ALGO, "comm point start listening %d", 
		c->fd==-1?newfd:c->fd);
	if(c->type == comm_tcp_accept && !c->tcp_free) {
		/* no use to start listening no free slots. */
		return;
	}
	if(msec != -1 && msec != 0) {
		if(!c->timeout) {
			c->timeout = (struct timeval*)malloc(sizeof(
				struct timeval));
			if(!c->timeout) {
				log_err("cpsl: malloc failed. No net read.");
				return;
			}
		}
		ub_event_add_bits(c->ev->ev, UB_EV_TIMEOUT);
#ifndef S_SPLINT_S /* splint fails on struct timeval. */
		c->timeout->tv_sec = msec/1000;
		c->timeout->tv_usec = (msec%1000)*1000;
#endif /* S_SPLINT_S */
	}
	if(c->type == comm_tcp || c->type == comm_http) {
		ub_event_del_bits(c->ev->ev, UB_EV_READ|UB_EV_WRITE);
		if(c->tcp_is_reading)
			ub_event_add_bits(c->ev->ev, UB_EV_READ);
		else	ub_event_add_bits(c->ev->ev, UB_EV_WRITE);
	}
	if(newfd != -1) {
		if(c->fd != -1) {
#ifndef USE_WINSOCK
			close(c->fd);
#else
			closesocket(c->fd);
#endif
		}
		c->fd = newfd;
		ub_event_set_fd(c->ev->ev, c->fd);
	}
	if(ub_event_add(c->ev->ev, msec==0?NULL:c->timeout) != 0) {
		log_err("event_add failed. in cpsl.");
	}
}

void comm_point_listen_for_rw(struct comm_point* c, int rd, int wr)
{
	verbose(VERB_ALGO, "comm point listen_for_rw %d %d", c->fd, wr);
	if(ub_event_del(c->ev->ev) != 0) {
		log_err("event_del error to cplf");
	}
	ub_event_del_bits(c->ev->ev, UB_EV_READ|UB_EV_WRITE);
	if(rd) ub_event_add_bits(c->ev->ev, UB_EV_READ);
	if(wr) ub_event_add_bits(c->ev->ev, UB_EV_WRITE);
	if(ub_event_add(c->ev->ev, c->timeout) != 0) {
		log_err("event_add failed. in cplf.");
	}
}

size_t comm_point_get_mem(struct comm_point* c)
{
	size_t s;
	if(!c) 
		return 0;
	s = sizeof(*c) + sizeof(*c->ev);
	if(c->timeout) 
		s += sizeof(*c->timeout);
	if(c->type == comm_tcp || c->type == comm_local) {
		s += sizeof(*c->buffer) + sldns_buffer_capacity(c->buffer);
#ifdef USE_DNSCRYPT
		s += sizeof(*c->dnscrypt_buffer);
		if(c->buffer != c->dnscrypt_buffer) {
			s += sldns_buffer_capacity(c->dnscrypt_buffer);
		}
#endif
	}
	if(c->type == comm_tcp_accept) {
		int i;
		for(i=0; i<c->max_tcp_count; i++)
			s += comm_point_get_mem(c->tcp_handlers[i]);
	}
	return s;
}

struct comm_timer* 
comm_timer_create(struct comm_base* base, void (*cb)(void*), void* cb_arg)
{
	struct internal_timer *tm = (struct internal_timer*)calloc(1,
		sizeof(struct internal_timer));
	if(!tm) {
		log_err("malloc failed");
		return NULL;
	}
	tm->super.ev_timer = tm;
	tm->base = base;
	tm->super.callback = cb;
	tm->super.cb_arg = cb_arg;
	tm->ev = ub_event_new(base->eb->base, -1, UB_EV_TIMEOUT, 
		comm_timer_callback, &tm->super);
	if(tm->ev == NULL) {
		log_err("timer_create: event_base_set failed.");
		free(tm);
		return NULL;
	}
	return &tm->super;
}

void 
comm_timer_disable(struct comm_timer* timer)
{
	if(!timer)
		return;
	ub_timer_del(timer->ev_timer->ev);
	timer->ev_timer->enabled = 0;
}

void 
comm_timer_set(struct comm_timer* timer, struct timeval* tv)
{
	log_assert(tv);
	if(timer->ev_timer->enabled)
		comm_timer_disable(timer);
	if(ub_timer_add(timer->ev_timer->ev, timer->ev_timer->base->eb->base,
		comm_timer_callback, timer, tv) != 0)
		log_err("comm_timer_set: evtimer_add failed.");
	timer->ev_timer->enabled = 1;
}

void 
comm_timer_delete(struct comm_timer* timer)
{
	if(!timer)
		return;
	comm_timer_disable(timer);
	/* Free the sub struct timer->ev_timer derived from the super struct timer.
	 * i.e. assert(timer == timer->ev_timer)
	 */
	ub_event_free(timer->ev_timer->ev);
	free(timer->ev_timer);
}

void 
comm_timer_callback(int ATTR_UNUSED(fd), short event, void* arg)
{
	struct comm_timer* tm = (struct comm_timer*)arg;
	if(!(event&UB_EV_TIMEOUT))
		return;
	ub_comm_base_now(tm->ev_timer->base);
	tm->ev_timer->enabled = 0;
	fptr_ok(fptr_whitelist_comm_timer(tm->callback));
	(*tm->callback)(tm->cb_arg);
}

int 
comm_timer_is_set(struct comm_timer* timer)
{
	return (int)timer->ev_timer->enabled;
}

size_t 
comm_timer_get_mem(struct comm_timer* ATTR_UNUSED(timer))
{
	return sizeof(struct internal_timer);
}

struct comm_signal* 
comm_signal_create(struct comm_base* base,
        void (*callback)(int, void*), void* cb_arg)
{
	struct comm_signal* com = (struct comm_signal*)malloc(
		sizeof(struct comm_signal));
	if(!com) {
		log_err("malloc failed");
		return NULL;
	}
	com->base = base;
	com->callback = callback;
	com->cb_arg = cb_arg;
	com->ev_signal = NULL;
	return com;
}

void 
comm_signal_callback(int sig, short event, void* arg)
{
	struct comm_signal* comsig = (struct comm_signal*)arg;
	if(!(event & UB_EV_SIGNAL))
		return;
	ub_comm_base_now(comsig->base);
	fptr_ok(fptr_whitelist_comm_signal(comsig->callback));
	(*comsig->callback)(sig, comsig->cb_arg);
}

int 
comm_signal_bind(struct comm_signal* comsig, int sig)
{
	struct internal_signal* entry = (struct internal_signal*)calloc(1, 
		sizeof(struct internal_signal));
	if(!entry) {
		log_err("malloc failed");
		return 0;
	}
	log_assert(comsig);
	/* add signal event */
	entry->ev = ub_signal_new(comsig->base->eb->base, sig,
		comm_signal_callback, comsig);
	if(entry->ev == NULL) {
		log_err("Could not create signal event");
		free(entry);
		return 0;
	}
	if(ub_signal_add(entry->ev, NULL) != 0) {
		log_err("Could not add signal handler");
		ub_event_free(entry->ev);
		free(entry);
		return 0;
	}
	/* link into list */
	entry->next = comsig->ev_signal;
	comsig->ev_signal = entry;
	return 1;
}

void 
comm_signal_delete(struct comm_signal* comsig)
{
	struct internal_signal* p, *np;
	if(!comsig)
		return;
	p=comsig->ev_signal;
	while(p) {
		np = p->next;
		ub_signal_del(p->ev);
		ub_event_free(p->ev);
		free(p);
		p = np;
	}
	free(comsig);
}
