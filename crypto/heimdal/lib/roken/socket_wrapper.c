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

/*
   Socket wrapper library. Passes all socket communication over
   unix domain sockets if the environment variable SOCKET_WRAPPER_DIR
   is set.
*/

#define SOCKET_WRAPPER_NOT_REPLACE

#ifdef _SAMBA_BUILD_

#include "includes.h"
#include "system/network.h"
#include "system/filesys.h"

#ifdef malloc
#undef malloc
#endif
#ifdef calloc
#undef calloc
#endif
#ifdef strdup
#undef strdup
#endif

#else /* _SAMBA_BUILD_ */

#include <config.h>
#undef SOCKET_WRAPPER_REPLACE

#include <sys/types.h>
#ifdef TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#elif defined(HAVE_SYS_TIME_H)
#include <sys/time.h>
#else
#include <time.h>
#endif
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
#include <errno.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "roken.h"

#include "socket_wrapper.h"

#define HAVE_GETTIMEOFDAY_TZ 1

#define _PUBLIC_

#endif

#define SWRAP_DLIST_ADD(list,item) do { \
	if (!(list)) { \
		(item)->prev	= NULL; \
		(item)->next	= NULL; \
		(list)		= (item); \
	} else { \
		(item)->prev	= NULL; \
		(item)->next	= (list); \
		(list)->prev	= (item); \
		(list)		= (item); \
	} \
} while (0)

#define SWRAP_DLIST_REMOVE(list,item) do { \
	if ((list) == (item)) { \
		(list)		= (item)->next; \
		if (list) { \
			(list)->prev	= NULL; \
		} \
	} else { \
		if ((item)->prev) { \
			(item)->prev->next	= (item)->next; \
		} \
		if ((item)->next) { \
			(item)->next->prev	= (item)->prev; \
		} \
	} \
	(item)->prev	= NULL; \
	(item)->next	= NULL; \
} while (0)

/* LD_PRELOAD doesn't work yet, so REWRITE_CALLS is all we support
 * for now */
#define REWRITE_CALLS

#ifdef REWRITE_CALLS
#define real_accept accept
#define real_connect connect
#define real_bind bind
#define real_listen listen
#define real_getpeername getpeername
#define real_getsockname getsockname
#define real_getsockopt getsockopt
#define real_setsockopt setsockopt
#define real_recvfrom recvfrom
#define real_sendto sendto
#define real_ioctl ioctl
#define real_recv recv
#define real_send send
#define real_socket socket
#define real_close close
#define real_dup dup
#define real_dup2 dup2
#endif

#ifdef HAVE_GETTIMEOFDAY_TZ
#define swrapGetTimeOfDay(tval) gettimeofday(tval,NULL)
#else
#define swrapGetTimeOfDay(tval)	gettimeofday(tval)
#endif

/* we need to use a very terse format here as IRIX 6.4 silently
   truncates names to 16 chars, so if we use a longer name then we
   can't tell which port a packet came from with recvfrom()

   with this format we have 8 chars left for the directory name
*/
#define SOCKET_FORMAT "%c%02X%04X"
#define SOCKET_TYPE_CHAR_TCP		'T'
#define SOCKET_TYPE_CHAR_UDP		'U'
#define SOCKET_TYPE_CHAR_TCP_V6		'X'
#define SOCKET_TYPE_CHAR_UDP_V6		'Y'

#define MAX_WRAPPED_INTERFACES 16

#define SW_IPV6_ADDRESS 1

static struct sockaddr *sockaddr_dup(const void *data, socklen_t len)
{
	struct sockaddr *ret = (struct sockaddr *)malloc(len);
	memcpy(ret, data, len);
	return ret;
}

static void set_port(int family, int prt, struct sockaddr *addr)
{
	switch (family) {
	case AF_INET:
		((struct sockaddr_in *)addr)->sin_port = htons(prt);
		break;
#ifdef HAVE_IPV6
	case AF_INET6:
		((struct sockaddr_in6 *)addr)->sin6_port = htons(prt);
		break;
#endif
	}
}

static int socket_length(int family)
{
	switch (family) {
	case AF_INET:
		return sizeof(struct sockaddr_in);
#ifdef HAVE_IPV6
	case AF_INET6:
		return sizeof(struct sockaddr_in6);
#endif
	}
	return -1;
}



struct socket_info
{
	int fd;

	int family;
	int type;
	int protocol;
	int bound;
	int bcast;
	int is_server;

	char *path;
	char *tmp_path;

	struct sockaddr *myname;
	socklen_t myname_len;

	struct sockaddr *peername;
	socklen_t peername_len;

	struct {
		unsigned long pck_snd;
		unsigned long pck_rcv;
	} io;

	struct socket_info *prev, *next;
};

static struct socket_info *sockets;


static const char *socket_wrapper_dir(void)
{
	const char *s = getenv("SOCKET_WRAPPER_DIR");
	if (s == NULL) {
		return NULL;
	}
	if (strncmp(s, "./", 2) == 0) {
		s += 2;
	}
	return s;
}

static unsigned int socket_wrapper_default_iface(void)
{
	const char *s = getenv("SOCKET_WRAPPER_DEFAULT_IFACE");
	if (s) {
		unsigned int iface;
		if (sscanf(s, "%u", &iface) == 1) {
			if (iface >= 1 && iface <= MAX_WRAPPED_INTERFACES) {
				return iface;
			}
		}
	}

	return 1;/* 127.0.0.1 */
}

static int convert_un_in(const struct sockaddr_un *un, struct sockaddr *in, socklen_t *len)
{
	unsigned int iface;
	unsigned int prt;
	const char *p;
	char type;

	p = strrchr(un->sun_path, '/');
	if (p) p++; else p = un->sun_path;

	if (sscanf(p, SOCKET_FORMAT, &type, &iface, &prt) != 3) {
		errno = EINVAL;
		return -1;
	}

	if (iface == 0 || iface > MAX_WRAPPED_INTERFACES) {
		errno = EINVAL;
		return -1;
	}

	if (prt > 0xFFFF) {
		errno = EINVAL;
		return -1;
	}

	switch(type) {
	case SOCKET_TYPE_CHAR_TCP:
	case SOCKET_TYPE_CHAR_UDP: {
		struct sockaddr_in *in2 = (struct sockaddr_in *)in;

		if ((*len) < sizeof(*in2)) {
		    errno = EINVAL;
		    return -1;
		}

		memset(in2, 0, sizeof(*in2));
		in2->sin_family = AF_INET;
		in2->sin_addr.s_addr = htonl((127<<24) | iface);
		in2->sin_port = htons(prt);

		*len = sizeof(*in2);
		break;
	}
#ifdef HAVE_IPV6
	case SOCKET_TYPE_CHAR_TCP_V6:
	case SOCKET_TYPE_CHAR_UDP_V6: {
		struct sockaddr_in6 *in2 = (struct sockaddr_in6 *)in;

		if ((*len) < sizeof(*in2)) {
			errno = EINVAL;
			return -1;
		}

		memset(in2, 0, sizeof(*in2));
		in2->sin6_family = AF_INET6;
		in2->sin6_addr.s6_addr[0] = SW_IPV6_ADDRESS;
		in2->sin6_port = htons(prt);

		*len = sizeof(*in2);
		break;
	}
#endif
	default:
		errno = EINVAL;
		return -1;
	}

	return 0;
}

static int convert_in_un_remote(struct socket_info *si, const struct sockaddr *inaddr, struct sockaddr_un *un,
				int *bcast)
{
	char type = '\0';
	unsigned int prt;
	unsigned int iface;
	int is_bcast = 0;

	if (bcast) *bcast = 0;

	switch (si->family) {
	case AF_INET: {
		const struct sockaddr_in *in =
		    (const struct sockaddr_in *)inaddr;
		unsigned int addr = ntohl(in->sin_addr.s_addr);
		char u_type = '\0';
		char b_type = '\0';
		char a_type = '\0';

		switch (si->type) {
		case SOCK_STREAM:
			u_type = SOCKET_TYPE_CHAR_TCP;
			break;
		case SOCK_DGRAM:
			u_type = SOCKET_TYPE_CHAR_UDP;
			a_type = SOCKET_TYPE_CHAR_UDP;
			b_type = SOCKET_TYPE_CHAR_UDP;
			break;
		}

		prt = ntohs(in->sin_port);
		if (a_type && addr == 0xFFFFFFFF) {
			/* 255.255.255.255 only udp */
			is_bcast = 2;
			type = a_type;
			iface = socket_wrapper_default_iface();
		} else if (b_type && addr == 0x7FFFFFFF) {
			/* 127.255.255.255 only udp */
			is_bcast = 1;
			type = b_type;
			iface = socket_wrapper_default_iface();
		} else if ((addr & 0xFFFFFF00) == 0x7F000000) {
			/* 127.0.0.X */
			is_bcast = 0;
			type = u_type;
			iface = (addr & 0x000000FF);
		} else {
			errno = ENETUNREACH;
			return -1;
		}
		if (bcast) *bcast = is_bcast;
		break;
	}
#ifdef HAVE_IPV6
	case AF_INET6: {
		const struct sockaddr_in6 *in =
		    (const struct sockaddr_in6 *)inaddr;

		switch (si->type) {
		case SOCK_STREAM:
			type = SOCKET_TYPE_CHAR_TCP_V6;
			break;
		case SOCK_DGRAM:
			type = SOCKET_TYPE_CHAR_UDP_V6;
			break;
		}

		/* XXX no multicast/broadcast */

		prt = ntohs(in->sin6_port);
		iface = SW_IPV6_ADDRESS;

		break;
	}
#endif
	default:
		errno = ENETUNREACH;
		return -1;
	}

	if (prt == 0) {
		errno = EINVAL;
		return -1;
	}

	if (is_bcast) {
		snprintf(un->sun_path, sizeof(un->sun_path), "%s/EINVAL",
			 socket_wrapper_dir());
		/* the caller need to do more processing */
		return 0;
	}

	snprintf(un->sun_path, sizeof(un->sun_path), "%s/"SOCKET_FORMAT,
		 socket_wrapper_dir(), type, iface, prt);

	return 0;
}

static int convert_in_un_alloc(struct socket_info *si, const struct sockaddr *inaddr, struct sockaddr_un *un,
			       int *bcast)
{
	char type = '\0';
	unsigned int prt;
	unsigned int iface;
	struct stat st;
	int is_bcast = 0;

	if (bcast) *bcast = 0;

	switch (si->family) {
	case AF_INET: {
		const struct sockaddr_in *in =
		    (const struct sockaddr_in *)inaddr;
		unsigned int addr = ntohl(in->sin_addr.s_addr);
		char u_type = '\0';
		char d_type = '\0';
		char b_type = '\0';
		char a_type = '\0';

		prt = ntohs(in->sin_port);

		switch (si->type) {
		case SOCK_STREAM:
			u_type = SOCKET_TYPE_CHAR_TCP;
			d_type = SOCKET_TYPE_CHAR_TCP;
			break;
		case SOCK_DGRAM:
			u_type = SOCKET_TYPE_CHAR_UDP;
			d_type = SOCKET_TYPE_CHAR_UDP;
			a_type = SOCKET_TYPE_CHAR_UDP;
			b_type = SOCKET_TYPE_CHAR_UDP;
			break;
		}

		if (addr == 0) {
			/* 0.0.0.0 */
		 	is_bcast = 0;
			type = d_type;
			iface = socket_wrapper_default_iface();
		} else if (a_type && addr == 0xFFFFFFFF) {
			/* 255.255.255.255 only udp */
			is_bcast = 2;
			type = a_type;
			iface = socket_wrapper_default_iface();
		} else if (b_type && addr == 0x7FFFFFFF) {
			/* 127.255.255.255 only udp */
			is_bcast = 1;
			type = b_type;
			iface = socket_wrapper_default_iface();
		} else if ((addr & 0xFFFFFF00) == 0x7F000000) {
			/* 127.0.0.X */
			is_bcast = 0;
			type = u_type;
			iface = (addr & 0x000000FF);
		} else {
			errno = EADDRNOTAVAIL;
			return -1;
		}
		break;
	}
#ifdef HAVE_IPV6
	case AF_INET6: {
		const struct sockaddr_in6 *in =
		    (const struct sockaddr_in6 *)inaddr;

		switch (si->type) {
		case SOCK_STREAM:
			type = SOCKET_TYPE_CHAR_TCP_V6;
			break;
		case SOCK_DGRAM:
			type = SOCKET_TYPE_CHAR_UDP_V6;
			break;
		}

		/* XXX no multicast/broadcast */

		prt = ntohs(in->sin6_port);
		iface = SW_IPV6_ADDRESS;

		break;
	}
#endif
	default:
		errno = ENETUNREACH;
		return -1;
	}


	if (bcast) *bcast = is_bcast;

	if (prt == 0) {
		/* handle auto-allocation of ephemeral ports */
		for (prt = 5001; prt < 10000; prt++) {
			snprintf(un->sun_path, sizeof(un->sun_path), "%s/"SOCKET_FORMAT,
				 socket_wrapper_dir(), type, iface, prt);
			if (stat(un->sun_path, &st) == 0) continue;

			set_port(si->family, prt, si->myname);
		}
	}

	snprintf(un->sun_path, sizeof(un->sun_path), "%s/"SOCKET_FORMAT,
		 socket_wrapper_dir(), type, iface, prt);
	return 0;
}

static struct socket_info *find_socket_info(int fd)
{
	struct socket_info *i;
	for (i = sockets; i; i = i->next) {
		if (i->fd == fd)
			return i;
	}

	return NULL;
}

static int sockaddr_convert_to_un(struct socket_info *si, const struct sockaddr *in_addr, socklen_t in_len,
				  struct sockaddr_un *out_addr, int alloc_sock, int *bcast)
{
	if (!out_addr)
		return 0;

	out_addr->sun_family = AF_UNIX;

	switch (in_addr->sa_family) {
	case AF_INET:
#ifdef HAVE_IPV6
	case AF_INET6:
#endif
		switch (si->type) {
		case SOCK_STREAM:
		case SOCK_DGRAM:
			break;
		default:
			errno = ESOCKTNOSUPPORT;
			return -1;
		}
		if (alloc_sock) {
			return convert_in_un_alloc(si, in_addr, out_addr, bcast);
		} else {
			return convert_in_un_remote(si, in_addr, out_addr, bcast);
		}
	default:
		break;
	}

	errno = EAFNOSUPPORT;
	return -1;
}

static int sockaddr_convert_from_un(const struct socket_info *si,
				    const struct sockaddr_un *in_addr,
				    socklen_t un_addrlen,
				    int family,
				    struct sockaddr *out_addr,
				    socklen_t *out_addrlen)
{
	if (out_addr == NULL || out_addrlen == NULL)
		return 0;

	if (un_addrlen == 0) {
		*out_addrlen = 0;
		return 0;
	}

	switch (family) {
	case AF_INET:
#ifdef HAVE_IPV6
	case AF_INET6:
#endif
		switch (si->type) {
		case SOCK_STREAM:
		case SOCK_DGRAM:
			break;
		default:
			errno = ESOCKTNOSUPPORT;
			return -1;
		}
		return convert_un_in(in_addr, out_addr, out_addrlen);
	default:
		break;
	}

	errno = EAFNOSUPPORT;
	return -1;
}

enum swrap_packet_type {
	SWRAP_CONNECT_SEND,
	SWRAP_CONNECT_UNREACH,
	SWRAP_CONNECT_RECV,
	SWRAP_CONNECT_ACK,
	SWRAP_ACCEPT_SEND,
	SWRAP_ACCEPT_RECV,
	SWRAP_ACCEPT_ACK,
	SWRAP_RECVFROM,
	SWRAP_SENDTO,
	SWRAP_SENDTO_UNREACH,
	SWRAP_PENDING_RST,
	SWRAP_RECV,
	SWRAP_RECV_RST,
	SWRAP_SEND,
	SWRAP_SEND_RST,
	SWRAP_CLOSE_SEND,
	SWRAP_CLOSE_RECV,
	SWRAP_CLOSE_ACK
};

struct swrap_file_hdr {
	unsigned long	magic;
	unsigned short	version_major;
	unsigned short	version_minor;
	long		timezone;
	unsigned long	sigfigs;
	unsigned long	frame_max_len;
#define SWRAP_FRAME_LENGTH_MAX 0xFFFF
	unsigned long	link_type;
};
#define SWRAP_FILE_HDR_SIZE 24

struct swrap_packet {
	struct {
		unsigned long seconds;
		unsigned long micro_seconds;
		unsigned long recorded_length;
		unsigned long full_length;
	} frame;
#define SWRAP_PACKET__FRAME_SIZE 16

	struct {
		struct {
			unsigned char	ver_hdrlen;
			unsigned char	tos;
			unsigned short	packet_length;
			unsigned short	identification;
			unsigned char	flags;
			unsigned char	fragment;
			unsigned char	ttl;
			unsigned char	protocol;
			unsigned short	hdr_checksum;
			unsigned long	src_addr;
			unsigned long	dest_addr;
		} hdr;
#define SWRAP_PACKET__IP_HDR_SIZE 20

		union {
			struct {
				unsigned short	source_port;
				unsigned short	dest_port;
				unsigned long	seq_num;
				unsigned long	ack_num;
				unsigned char	hdr_length;
				unsigned char	control;
				unsigned short	window;
				unsigned short	checksum;
				unsigned short	urg;
			} tcp;
#define SWRAP_PACKET__IP_P_TCP_SIZE 20
			struct {
				unsigned short	source_port;
				unsigned short	dest_port;
				unsigned short	length;
				unsigned short	checksum;
			} udp;
#define SWRAP_PACKET__IP_P_UDP_SIZE 8
			struct {
				unsigned char	type;
				unsigned char	code;
				unsigned short	checksum;
				unsigned long	unused;
			} icmp;
#define SWRAP_PACKET__IP_P_ICMP_SIZE 8
		} p;
	} ip;
};
#define SWRAP_PACKET_SIZE 56

static const char *socket_wrapper_pcap_file(void)
{
	static int initialized = 0;
	static const char *s = NULL;
	static const struct swrap_file_hdr h;
	static const struct swrap_packet p;

	if (initialized == 1) {
		return s;
	}
	initialized = 1;

	/*
	 * TODO: don't use the structs use plain buffer offsets
	 *       and PUSH_U8(), PUSH_U16() and PUSH_U32()
	 *
	 * for now make sure we disable PCAP support
	 * if the struct has alignment!
	 */
	if (sizeof(h) != SWRAP_FILE_HDR_SIZE) {
		return NULL;
	}
	if (sizeof(p) != SWRAP_PACKET_SIZE) {
		return NULL;
	}
	if (sizeof(p.frame) != SWRAP_PACKET__FRAME_SIZE) {
		return NULL;
	}
	if (sizeof(p.ip.hdr) != SWRAP_PACKET__IP_HDR_SIZE) {
		return NULL;
	}
	if (sizeof(p.ip.p.tcp) != SWRAP_PACKET__IP_P_TCP_SIZE) {
		return NULL;
	}
	if (sizeof(p.ip.p.udp) != SWRAP_PACKET__IP_P_UDP_SIZE) {
		return NULL;
	}
	if (sizeof(p.ip.p.icmp) != SWRAP_PACKET__IP_P_ICMP_SIZE) {
		return NULL;
	}

	s = getenv("SOCKET_WRAPPER_PCAP_FILE");
	if (s == NULL) {
		return NULL;
	}
	if (strncmp(s, "./", 2) == 0) {
		s += 2;
	}
	return s;
}

static struct swrap_packet *swrap_packet_init(struct timeval *tval,
					      const struct sockaddr_in *src_addr,
					      const struct sockaddr_in *dest_addr,
					      int socket_type,
					      const unsigned char *payload,
					      size_t payload_len,
					      unsigned long tcp_seq,
					      unsigned long tcp_ack,
					      unsigned char tcp_ctl,
					      int unreachable,
					      size_t *_packet_len)
{
	struct swrap_packet *ret;
	struct swrap_packet *packet;
	size_t packet_len;
	size_t alloc_len;
	size_t nonwire_len = sizeof(packet->frame);
	size_t wire_hdr_len = 0;
	size_t wire_len = 0;
	size_t icmp_hdr_len = 0;
	size_t icmp_truncate_len = 0;
	unsigned char protocol = 0, icmp_protocol = 0;
	unsigned short src_port = src_addr->sin_port;
	unsigned short dest_port = dest_addr->sin_port;

	switch (socket_type) {
	case SOCK_STREAM:
		protocol = 0x06; /* TCP */
		wire_hdr_len = sizeof(packet->ip.hdr) + sizeof(packet->ip.p.tcp);
		wire_len = wire_hdr_len + payload_len;
		break;

	case SOCK_DGRAM:
		protocol = 0x11; /* UDP */
		wire_hdr_len = sizeof(packet->ip.hdr) + sizeof(packet->ip.p.udp);
		wire_len = wire_hdr_len + payload_len;
		break;
	}

	if (unreachable) {
		icmp_protocol = protocol;
		protocol = 0x01; /* ICMP */
		if (wire_len > 64 ) {
			icmp_truncate_len = wire_len - 64;
		}
		icmp_hdr_len = sizeof(packet->ip.hdr) + sizeof(packet->ip.p.icmp);
		wire_hdr_len += icmp_hdr_len;
		wire_len += icmp_hdr_len;
	}

	packet_len = nonwire_len + wire_len;
	alloc_len = packet_len;
	if (alloc_len < sizeof(struct swrap_packet)) {
		alloc_len = sizeof(struct swrap_packet);
	}
	ret = (struct swrap_packet *)malloc(alloc_len);
	if (!ret) return NULL;

	packet = ret;

	packet->frame.seconds		= tval->tv_sec;
	packet->frame.micro_seconds	= tval->tv_usec;
	packet->frame.recorded_length	= wire_len - icmp_truncate_len;
	packet->frame.full_length	= wire_len - icmp_truncate_len;

	packet->ip.hdr.ver_hdrlen	= 0x45; /* version 4 and 5 * 32 bit words */
	packet->ip.hdr.tos		= 0x00;
	packet->ip.hdr.packet_length	= htons(wire_len - icmp_truncate_len);
	packet->ip.hdr.identification	= htons(0xFFFF);
	packet->ip.hdr.flags		= 0x40; /* BIT 1 set - means don't fraqment */
	packet->ip.hdr.fragment		= htons(0x0000);
	packet->ip.hdr.ttl		= 0xFF;
	packet->ip.hdr.protocol		= protocol;
	packet->ip.hdr.hdr_checksum	= htons(0x0000);
	packet->ip.hdr.src_addr		= src_addr->sin_addr.s_addr;
	packet->ip.hdr.dest_addr	= dest_addr->sin_addr.s_addr;

	if (unreachable) {
		packet->ip.p.icmp.type		= 0x03; /* destination unreachable */
		packet->ip.p.icmp.code		= 0x01; /* host unreachable */
		packet->ip.p.icmp.checksum	= htons(0x0000);
		packet->ip.p.icmp.unused	= htonl(0x00000000);

		/* set the ip header in the ICMP payload */
		packet = (struct swrap_packet *)(((unsigned char *)ret) + icmp_hdr_len);
		packet->ip.hdr.ver_hdrlen	= 0x45; /* version 4 and 5 * 32 bit words */
		packet->ip.hdr.tos		= 0x00;
		packet->ip.hdr.packet_length	= htons(wire_len - icmp_hdr_len);
		packet->ip.hdr.identification	= htons(0xFFFF);
		packet->ip.hdr.flags		= 0x40; /* BIT 1 set - means don't fraqment */
		packet->ip.hdr.fragment		= htons(0x0000);
		packet->ip.hdr.ttl		= 0xFF;
		packet->ip.hdr.protocol		= icmp_protocol;
		packet->ip.hdr.hdr_checksum	= htons(0x0000);
		packet->ip.hdr.src_addr		= dest_addr->sin_addr.s_addr;
		packet->ip.hdr.dest_addr	= src_addr->sin_addr.s_addr;

		src_port = dest_addr->sin_port;
		dest_port = src_addr->sin_port;
	}

	switch (socket_type) {
	case SOCK_STREAM:
		packet->ip.p.tcp.source_port	= src_port;
		packet->ip.p.tcp.dest_port	= dest_port;
		packet->ip.p.tcp.seq_num	= htonl(tcp_seq);
		packet->ip.p.tcp.ack_num	= htonl(tcp_ack);
		packet->ip.p.tcp.hdr_length	= 0x50; /* 5 * 32 bit words */
		packet->ip.p.tcp.control	= tcp_ctl;
		packet->ip.p.tcp.window		= htons(0x7FFF);
		packet->ip.p.tcp.checksum	= htons(0x0000);
		packet->ip.p.tcp.urg		= htons(0x0000);

		break;

	case SOCK_DGRAM:
		packet->ip.p.udp.source_port	= src_addr->sin_port;
		packet->ip.p.udp.dest_port	= dest_addr->sin_port;
		packet->ip.p.udp.length		= htons(8 + payload_len);
		packet->ip.p.udp.checksum	= htons(0x0000);

		break;
	}

	if (payload && payload_len > 0) {
		unsigned char *p = (unsigned char *)ret;
		p += nonwire_len;
		p += wire_hdr_len;
		memcpy(p, payload, payload_len);
	}

	*_packet_len = packet_len - icmp_truncate_len;
	return ret;
}

static int swrap_get_pcap_fd(const char *fname)
{
	static int fd = -1;

	if (fd != -1) return fd;

	fd = open(fname, O_WRONLY|O_CREAT|O_EXCL|O_APPEND, 0644);
	if (fd != -1) {
		struct swrap_file_hdr file_hdr;
		file_hdr.magic		= 0xA1B2C3D4;
		file_hdr.version_major	= 0x0002;
		file_hdr.version_minor	= 0x0004;
		file_hdr.timezone	= 0x00000000;
		file_hdr.sigfigs	= 0x00000000;
		file_hdr.frame_max_len	= SWRAP_FRAME_LENGTH_MAX;
		file_hdr.link_type	= 0x0065; /* 101 RAW IP */

		write(fd, &file_hdr, sizeof(file_hdr));
		return fd;
	}

	fd = open(fname, O_WRONLY|O_APPEND, 0644);

	return fd;
}

static void swrap_dump_packet(struct socket_info *si, const struct sockaddr *addr,
			      enum swrap_packet_type type,
			      const void *buf, size_t len)
{
	const struct sockaddr_in *src_addr;
	const struct sockaddr_in *dest_addr;
	const char *file_name;
	unsigned long tcp_seq = 0;
	unsigned long tcp_ack = 0;
	unsigned char tcp_ctl = 0;
	int unreachable = 0;
	struct timeval tv;
	struct swrap_packet *packet;
	size_t packet_len = 0;
	int fd;

	file_name = socket_wrapper_pcap_file();
	if (!file_name) {
		return;
	}

	switch (si->family) {
	case AF_INET:
#ifdef HAVE_IPV6
	case AF_INET6:
#endif
		break;
	default:
		return;
	}

	switch (type) {
	case SWRAP_CONNECT_SEND:
		if (si->type != SOCK_STREAM) return;

		src_addr = (const struct sockaddr_in *)si->myname;
		dest_addr = (const struct sockaddr_in *)addr;

		tcp_seq = si->io.pck_snd;
		tcp_ack = si->io.pck_rcv;
		tcp_ctl = 0x02; /* SYN */

		si->io.pck_snd += 1;

		break;

	case SWRAP_CONNECT_RECV:
		if (si->type != SOCK_STREAM) return;

		dest_addr = (const struct sockaddr_in *)si->myname;
		src_addr = (const struct sockaddr_in *)addr;

		tcp_seq = si->io.pck_rcv;
		tcp_ack = si->io.pck_snd;
		tcp_ctl = 0x12; /** SYN,ACK */

		si->io.pck_rcv += 1;

		break;

	case SWRAP_CONNECT_UNREACH:
		if (si->type != SOCK_STREAM) return;

		dest_addr = (const struct sockaddr_in *)si->myname;
		src_addr = (const struct sockaddr_in *)addr;

		/* Unreachable: resend the data of SWRAP_CONNECT_SEND */
		tcp_seq = si->io.pck_snd - 1;
		tcp_ack = si->io.pck_rcv;
		tcp_ctl = 0x02; /* SYN */
		unreachable = 1;

		break;

	case SWRAP_CONNECT_ACK:
		if (si->type != SOCK_STREAM) return;

		src_addr = (const struct sockaddr_in *)si->myname;
		dest_addr = (const struct sockaddr_in *)addr;

		tcp_seq = si->io.pck_snd;
		tcp_ack = si->io.pck_rcv;
		tcp_ctl = 0x10; /* ACK */

		break;

	case SWRAP_ACCEPT_SEND:
		if (si->type != SOCK_STREAM) return;

		dest_addr = (const struct sockaddr_in *)si->myname;
		src_addr = (const struct sockaddr_in *)addr;

		tcp_seq = si->io.pck_rcv;
		tcp_ack = si->io.pck_snd;
		tcp_ctl = 0x02; /* SYN */

		si->io.pck_rcv += 1;

		break;

	case SWRAP_ACCEPT_RECV:
		if (si->type != SOCK_STREAM) return;

		src_addr = (const struct sockaddr_in *)si->myname;
		dest_addr = (const struct sockaddr_in *)addr;

		tcp_seq = si->io.pck_snd;
		tcp_ack = si->io.pck_rcv;
		tcp_ctl = 0x12; /* SYN,ACK */

		si->io.pck_snd += 1;

		break;

	case SWRAP_ACCEPT_ACK:
		if (si->type != SOCK_STREAM) return;

		dest_addr = (const struct sockaddr_in *)si->myname;
		src_addr = (const struct sockaddr_in *)addr;

		tcp_seq = si->io.pck_rcv;
		tcp_ack = si->io.pck_snd;
		tcp_ctl = 0x10; /* ACK */

		break;

	case SWRAP_SEND:
		src_addr = (const struct sockaddr_in *)si->myname;
		dest_addr = (const struct sockaddr_in *)si->peername;

		tcp_seq = si->io.pck_snd;
		tcp_ack = si->io.pck_rcv;
		tcp_ctl = 0x18; /* PSH,ACK */

		si->io.pck_snd += len;

		break;

	case SWRAP_SEND_RST:
		dest_addr = (const struct sockaddr_in *)si->myname;
		src_addr = (const struct sockaddr_in *)si->peername;

		if (si->type == SOCK_DGRAM) {
			swrap_dump_packet(si, si->peername,
					  SWRAP_SENDTO_UNREACH,
			      		  buf, len);
			return;
		}

		tcp_seq = si->io.pck_rcv;
		tcp_ack = si->io.pck_snd;
		tcp_ctl = 0x14; /** RST,ACK */

		break;

	case SWRAP_PENDING_RST:
		dest_addr = (const struct sockaddr_in *)si->myname;
		src_addr = (const struct sockaddr_in *)si->peername;

		if (si->type == SOCK_DGRAM) {
			return;
		}

		tcp_seq = si->io.pck_rcv;
		tcp_ack = si->io.pck_snd;
		tcp_ctl = 0x14; /* RST,ACK */

		break;

	case SWRAP_RECV:
		dest_addr = (const struct sockaddr_in *)si->myname;
		src_addr = (const struct sockaddr_in *)si->peername;

		tcp_seq = si->io.pck_rcv;
		tcp_ack = si->io.pck_snd;
		tcp_ctl = 0x18; /* PSH,ACK */

		si->io.pck_rcv += len;

		break;

	case SWRAP_RECV_RST:
		dest_addr = (const struct sockaddr_in *)si->myname;
		src_addr = (const struct sockaddr_in *)si->peername;

		if (si->type == SOCK_DGRAM) {
			return;
		}

		tcp_seq = si->io.pck_rcv;
		tcp_ack = si->io.pck_snd;
		tcp_ctl = 0x14; /* RST,ACK */

		break;

	case SWRAP_SENDTO:
		src_addr = (const struct sockaddr_in *)si->myname;
		dest_addr = (const struct sockaddr_in *)addr;

		si->io.pck_snd += len;

		break;

	case SWRAP_SENDTO_UNREACH:
		dest_addr = (const struct sockaddr_in *)si->myname;
		src_addr = (const struct sockaddr_in *)addr;

		unreachable = 1;

		break;

	case SWRAP_RECVFROM:
		dest_addr = (const struct sockaddr_in *)si->myname;
		src_addr = (const struct sockaddr_in *)addr;

		si->io.pck_rcv += len;

		break;

	case SWRAP_CLOSE_SEND:
		if (si->type != SOCK_STREAM) return;

		src_addr = (const struct sockaddr_in *)si->myname;
		dest_addr = (const struct sockaddr_in *)si->peername;

		tcp_seq = si->io.pck_snd;
		tcp_ack = si->io.pck_rcv;
		tcp_ctl = 0x11; /* FIN, ACK */

		si->io.pck_snd += 1;

		break;

	case SWRAP_CLOSE_RECV:
		if (si->type != SOCK_STREAM) return;

		dest_addr = (const struct sockaddr_in *)si->myname;
		src_addr = (const struct sockaddr_in *)si->peername;

		tcp_seq = si->io.pck_rcv;
		tcp_ack = si->io.pck_snd;
		tcp_ctl = 0x11; /* FIN,ACK */

		si->io.pck_rcv += 1;

		break;

	case SWRAP_CLOSE_ACK:
		if (si->type != SOCK_STREAM) return;

		src_addr = (const struct sockaddr_in *)si->myname;
		dest_addr = (const struct sockaddr_in *)si->peername;

		tcp_seq = si->io.pck_snd;
		tcp_ack = si->io.pck_rcv;
		tcp_ctl = 0x10; /* ACK */

		break;
	default:
		return;
	}

	swrapGetTimeOfDay(&tv);

	packet = swrap_packet_init(&tv, src_addr, dest_addr, si->type,
				   (const unsigned char *)buf, len,
				   tcp_seq, tcp_ack, tcp_ctl, unreachable,
				   &packet_len);
	if (!packet) {
		return;
	}

	fd = swrap_get_pcap_fd(file_name);
	if (fd != -1) {
		write(fd, packet, packet_len);
	}

	free(packet);
}

_PUBLIC_ int swrap_socket(int family, int type, int protocol)
{
	struct socket_info *si;
	int fd;

	if (!socket_wrapper_dir()) {
		return real_socket(family, type, protocol);
	}

	switch (family) {
	case AF_INET:
#ifdef HAVE_IPV6
	case AF_INET6:
#endif
		break;
	case AF_UNIX:
		return real_socket(family, type, protocol);
	default:
		errno = EAFNOSUPPORT;
		return -1;
	}

	switch (type) {
	case SOCK_STREAM:
		break;
	case SOCK_DGRAM:
		break;
	default:
		errno = EPROTONOSUPPORT;
		return -1;
	}

#if 0
	switch (protocol) {
	case 0:
		break;
	default:
		errno = EPROTONOSUPPORT;
		return -1;
	}
#endif

	fd = real_socket(AF_UNIX, type, 0);

	if (fd == -1) return -1;

	si = (struct socket_info *)calloc(1, sizeof(struct socket_info));

	si->family = family;
	si->type = type;
	si->protocol = protocol;
	si->fd = fd;

	SWRAP_DLIST_ADD(sockets, si);

	return si->fd;
}

_PUBLIC_ int swrap_accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
	struct socket_info *parent_si, *child_si;
	int fd;
	struct sockaddr_un un_addr;
	socklen_t un_addrlen = sizeof(un_addr);
	struct sockaddr_un un_my_addr;
	socklen_t un_my_addrlen = sizeof(un_my_addr);
	struct sockaddr *my_addr;
	socklen_t my_addrlen, len;
	int ret;

	parent_si = find_socket_info(s);
	if (!parent_si) {
		return real_accept(s, addr, addrlen);
	}

	/*
	 * assume out sockaddr have the same size as the in parent
	 * socket family
	 */
	my_addrlen = socket_length(parent_si->family);
	if (my_addrlen < 0) {
		errno = EINVAL;
		return -1;
	}

	my_addr = malloc(my_addrlen);
	if (my_addr == NULL) {
		return -1;
	}

	memset(&un_addr, 0, sizeof(un_addr));
	memset(&un_my_addr, 0, sizeof(un_my_addr));

	ret = real_accept(s, (struct sockaddr *)&un_addr, &un_addrlen);
	if (ret == -1) {
		free(my_addr);
		return ret;
	}

	fd = ret;

	len = my_addrlen;
	ret = sockaddr_convert_from_un(parent_si, &un_addr, un_addrlen,
				       parent_si->family, my_addr, &len);
	if (ret == -1) {
		free(my_addr);
		close(fd);
		return ret;
	}

	child_si = (struct socket_info *)malloc(sizeof(struct socket_info));
	memset(child_si, 0, sizeof(*child_si));

	child_si->fd = fd;
	child_si->family = parent_si->family;
	child_si->type = parent_si->type;
	child_si->protocol = parent_si->protocol;
	child_si->bound = 1;
	child_si->is_server = 1;

	child_si->peername_len = len;
	child_si->peername = sockaddr_dup(my_addr, len);

	if (addr != NULL && addrlen != NULL) {
	    *addrlen = len;
	    if (*addrlen >= len)
		memcpy(addr, my_addr, len);
	    *addrlen = 0;
	}

	ret = real_getsockname(fd, (struct sockaddr *)&un_my_addr, &un_my_addrlen);
	if (ret == -1) {
		free(child_si);
		close(fd);
		return ret;
	}

	len = my_addrlen;
	ret = sockaddr_convert_from_un(child_si, &un_my_addr, un_my_addrlen,
				       child_si->family, my_addr, &len);
	if (ret == -1) {
		free(child_si);
		free(my_addr);
		close(fd);
		return ret;
	}

	child_si->myname_len = len;
	child_si->myname = sockaddr_dup(my_addr, len);
	free(my_addr);

	SWRAP_DLIST_ADD(sockets, child_si);

	swrap_dump_packet(child_si, addr, SWRAP_ACCEPT_SEND, NULL, 0);
	swrap_dump_packet(child_si, addr, SWRAP_ACCEPT_RECV, NULL, 0);
	swrap_dump_packet(child_si, addr, SWRAP_ACCEPT_ACK, NULL, 0);

	return fd;
}

static int autobind_start_init;
static int autobind_start;

/* using sendto() or connect() on an unbound socket would give the
   recipient no way to reply, as unlike UDP and TCP, a unix domain
   socket can't auto-assign emphemeral port numbers, so we need to
   assign it here */
static int swrap_auto_bind(struct socket_info *si)
{
	struct sockaddr_un un_addr;
	int i;
	char type;
	int ret;
	int port;
	struct stat st;

	if (autobind_start_init != 1) {
		autobind_start_init = 1;
		autobind_start = getpid();
		autobind_start %= 50000;
		autobind_start += 10000;
	}

	un_addr.sun_family = AF_UNIX;

	switch (si->family) {
	case AF_INET: {
		struct sockaddr_in in;

		switch (si->type) {
		case SOCK_STREAM:
			type = SOCKET_TYPE_CHAR_TCP;
			break;
		case SOCK_DGRAM:
		    	type = SOCKET_TYPE_CHAR_UDP;
			break;
		default:
		    errno = ESOCKTNOSUPPORT;
		    return -1;
		}

		memset(&in, 0, sizeof(in));
		in.sin_family = AF_INET;
		in.sin_addr.s_addr = htonl(127<<24 |
					   socket_wrapper_default_iface());

		si->myname_len = sizeof(in);
		si->myname = sockaddr_dup(&in, si->myname_len);
		break;
	}
#ifdef HAVE_IPV6
	case AF_INET6: {
		struct sockaddr_in6 in6;

		switch (si->type) {
		case SOCK_STREAM:
			type = SOCKET_TYPE_CHAR_TCP_V6;
			break;
		case SOCK_DGRAM:
		    	type = SOCKET_TYPE_CHAR_UDP_V6;
			break;
		default:
		    errno = ESOCKTNOSUPPORT;
		    return -1;
		}

		memset(&in6, 0, sizeof(in6));
		in6.sin6_family = AF_INET6;
		in6.sin6_addr.s6_addr[0] = SW_IPV6_ADDRESS;
		si->myname_len = sizeof(in6);
		si->myname = sockaddr_dup(&in6, si->myname_len);
		break;
	}
#endif
	default:
		errno = ESOCKTNOSUPPORT;
		return -1;
	}

	if (autobind_start > 60000) {
		autobind_start = 10000;
	}

	for (i=0;i<1000;i++) {
		port = autobind_start + i;
		snprintf(un_addr.sun_path, sizeof(un_addr.sun_path),
			 "%s/"SOCKET_FORMAT, socket_wrapper_dir(),
			 type, socket_wrapper_default_iface(), port);
		if (stat(un_addr.sun_path, &st) == 0) continue;

		ret = real_bind(si->fd, (struct sockaddr *)&un_addr, sizeof(un_addr));
		if (ret == -1) return ret;

		si->tmp_path = strdup(un_addr.sun_path);
		si->bound = 1;
		autobind_start = port + 1;
		break;
	}
	if (i == 1000) {
		errno = ENFILE;
		return -1;
	}

	set_port(si->family, port, si->myname);

	return 0;
}


_PUBLIC_ int swrap_connect(int s, const struct sockaddr *serv_addr, socklen_t addrlen)
{
	int ret;
	struct sockaddr_un un_addr;
	struct socket_info *si = find_socket_info(s);

	if (!si) {
		return real_connect(s, serv_addr, addrlen);
	}

	if (si->bound == 0) {
		ret = swrap_auto_bind(si);
		if (ret == -1) return -1;
	}

	if (si->family != serv_addr->sa_family) {
		errno = EINVAL;
		return -1;
	}

	ret = sockaddr_convert_to_un(si, (const struct sockaddr *)serv_addr, addrlen, &un_addr, 0, NULL);
	if (ret == -1) return -1;

	swrap_dump_packet(si, serv_addr, SWRAP_CONNECT_SEND, NULL, 0);

	ret = real_connect(s, (struct sockaddr *)&un_addr,
			   sizeof(struct sockaddr_un));

	/* to give better errors */
	if (ret == -1 && errno == ENOENT) {
		errno = EHOSTUNREACH;
	}

	if (ret == 0) {
		si->peername_len = addrlen;
		si->peername = sockaddr_dup(serv_addr, addrlen);

		swrap_dump_packet(si, serv_addr, SWRAP_CONNECT_RECV, NULL, 0);
		swrap_dump_packet(si, serv_addr, SWRAP_CONNECT_ACK, NULL, 0);
	} else {
		swrap_dump_packet(si, serv_addr, SWRAP_CONNECT_UNREACH, NULL, 0);
	}

	return ret;
}

_PUBLIC_ int swrap_bind(int s, const struct sockaddr *myaddr, socklen_t addrlen)
{
	int ret;
	struct sockaddr_un un_addr;
	struct socket_info *si = find_socket_info(s);

	if (!si) {
		return real_bind(s, myaddr, addrlen);
	}

	si->myname_len = addrlen;
	si->myname = sockaddr_dup(myaddr, addrlen);

	ret = sockaddr_convert_to_un(si, (const struct sockaddr *)myaddr, addrlen, &un_addr, 1, &si->bcast);
	if (ret == -1) return -1;

	unlink(un_addr.sun_path);

	ret = real_bind(s, (struct sockaddr *)&un_addr,
			sizeof(struct sockaddr_un));

	if (ret == 0) {
		si->bound = 1;
	}

	return ret;
}

_PUBLIC_ int swrap_listen(int s, int backlog)
{
	int ret;
	struct socket_info *si = find_socket_info(s);

	if (!si) {
		return real_listen(s, backlog);
	}

	ret = real_listen(s, backlog);

	return ret;
}

_PUBLIC_ int swrap_getpeername(int s, struct sockaddr *name, socklen_t *addrlen)
{
	struct socket_info *si = find_socket_info(s);

	if (!si) {
		return real_getpeername(s, name, addrlen);
	}

	if (!si->peername)
	{
		errno = ENOTCONN;
		return -1;
	}

	memcpy(name, si->peername, si->peername_len);
	*addrlen = si->peername_len;

	return 0;
}

_PUBLIC_ int swrap_getsockname(int s, struct sockaddr *name, socklen_t *addrlen)
{
	struct socket_info *si = find_socket_info(s);

	if (!si) {
		return real_getsockname(s, name, addrlen);
	}

	memcpy(name, si->myname, si->myname_len);
	*addrlen = si->myname_len;

	return 0;
}

_PUBLIC_ int swrap_getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen)
{
	struct socket_info *si = find_socket_info(s);

	if (!si) {
		return real_getsockopt(s, level, optname, optval, optlen);
	}

	if (level == SOL_SOCKET) {
		return real_getsockopt(s, level, optname, optval, optlen);
	}

	errno = ENOPROTOOPT;
	return -1;
}

_PUBLIC_ int swrap_setsockopt(int s, int  level,  int  optname,  const  void  *optval, socklen_t optlen)
{
	struct socket_info *si = find_socket_info(s);

	if (!si) {
		return real_setsockopt(s, level, optname, optval, optlen);
	}

	if (level == SOL_SOCKET) {
		return real_setsockopt(s, level, optname, optval, optlen);
	}

	switch (si->family) {
	case AF_INET:
		return 0;
	default:
		errno = ENOPROTOOPT;
		return -1;
	}
}

_PUBLIC_ ssize_t swrap_recvfrom(int s, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
	struct sockaddr_un un_addr;
	socklen_t un_addrlen = sizeof(un_addr);
	int ret;
	struct socket_info *si = find_socket_info(s);

	if (!si) {
		return real_recvfrom(s, buf, len, flags, from, fromlen);
	}

	/* irix 6.4 forgets to null terminate the sun_path string :-( */
	memset(&un_addr, 0, sizeof(un_addr));
	ret = real_recvfrom(s, buf, len, flags, (struct sockaddr *)&un_addr, &un_addrlen);
	if (ret == -1)
		return ret;

	if (sockaddr_convert_from_un(si, &un_addr, un_addrlen,
				     si->family, from, fromlen) == -1) {
		return -1;
	}

	swrap_dump_packet(si, from, SWRAP_RECVFROM, buf, ret);

	return ret;
}


_PUBLIC_ ssize_t swrap_sendto(int s, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen)
{
	struct sockaddr_un un_addr;
	int ret;
	struct socket_info *si = find_socket_info(s);
	int bcast = 0;

	if (!si) {
		return real_sendto(s, buf, len, flags, to, tolen);
	}

	switch (si->type) {
	case SOCK_STREAM:
		ret = real_send(s, buf, len, flags);
		break;
	case SOCK_DGRAM:
		if (si->bound == 0) {
			ret = swrap_auto_bind(si);
			if (ret == -1) return -1;
		}

		ret = sockaddr_convert_to_un(si, to, tolen, &un_addr, 0, &bcast);
		if (ret == -1) return -1;

		if (bcast) {
			struct stat st;
			unsigned int iface;
			unsigned int prt = ntohs(((const struct sockaddr_in *)to)->sin_port);
			char type;

			type = SOCKET_TYPE_CHAR_UDP;

			for(iface=0; iface <= MAX_WRAPPED_INTERFACES; iface++) {
				snprintf(un_addr.sun_path, sizeof(un_addr.sun_path), "%s/"SOCKET_FORMAT,
					 socket_wrapper_dir(), type, iface, prt);
				if (stat(un_addr.sun_path, &st) != 0) continue;

				/* ignore the any errors in broadcast sends */
				real_sendto(s, buf, len, flags, (struct sockaddr *)&un_addr, sizeof(un_addr));
			}

			swrap_dump_packet(si, to, SWRAP_SENDTO, buf, len);

			return len;
		}

		ret = real_sendto(s, buf, len, flags, (struct sockaddr *)&un_addr, sizeof(un_addr));
		break;
	default:
		ret = -1;
		errno = EHOSTUNREACH;
		break;
	}

	/* to give better errors */
	if (ret == -1 && errno == ENOENT) {
		errno = EHOSTUNREACH;
	}

	if (ret == -1) {
		swrap_dump_packet(si, to, SWRAP_SENDTO, buf, len);
		swrap_dump_packet(si, to, SWRAP_SENDTO_UNREACH, buf, len);
	} else {
		swrap_dump_packet(si, to, SWRAP_SENDTO, buf, ret);
	}

	return ret;
}

_PUBLIC_ int swrap_ioctl(int s, int r, void *p)
{
	int ret;
	struct socket_info *si = find_socket_info(s);
	int value;

	if (!si) {
		return real_ioctl(s, r, p);
	}

	ret = real_ioctl(s, r, p);

	switch (r) {
	case FIONREAD:
		value = *((int *)p);
		if (ret == -1 && errno != EAGAIN && errno != ENOBUFS) {
			swrap_dump_packet(si, NULL, SWRAP_PENDING_RST, NULL, 0);
		} else if (value == 0) { /* END OF FILE */
			swrap_dump_packet(si, NULL, SWRAP_PENDING_RST, NULL, 0);
		}
		break;
	}

	return ret;
}

_PUBLIC_ ssize_t swrap_recv(int s, void *buf, size_t len, int flags)
{
	int ret;
	struct socket_info *si = find_socket_info(s);

	if (!si) {
		return real_recv(s, buf, len, flags);
	}

	ret = real_recv(s, buf, len, flags);
	if (ret == -1 && errno != EAGAIN && errno != ENOBUFS) {
		swrap_dump_packet(si, NULL, SWRAP_RECV_RST, NULL, 0);
	} else if (ret == 0) { /* END OF FILE */
		swrap_dump_packet(si, NULL, SWRAP_RECV_RST, NULL, 0);
	} else {
		swrap_dump_packet(si, NULL, SWRAP_RECV, buf, ret);
	}

	return ret;
}


_PUBLIC_ ssize_t swrap_send(int s, const void *buf, size_t len, int flags)
{
	int ret;
	struct socket_info *si = find_socket_info(s);

	if (!si) {
		return real_send(s, buf, len, flags);
	}

	ret = real_send(s, buf, len, flags);

	if (ret == -1) {
		swrap_dump_packet(si, NULL, SWRAP_SEND, buf, len);
		swrap_dump_packet(si, NULL, SWRAP_SEND_RST, NULL, 0);
	} else {
		swrap_dump_packet(si, NULL, SWRAP_SEND, buf, ret);
	}

	return ret;
}

_PUBLIC_ int swrap_close(int fd)
{
	struct socket_info *si = find_socket_info(fd);
	int ret;

	if (!si) {
		return real_close(fd);
	}

	SWRAP_DLIST_REMOVE(sockets, si);

	if (si->myname && si->peername) {
		swrap_dump_packet(si, NULL, SWRAP_CLOSE_SEND, NULL, 0);
	}

	ret = real_close(fd);

	if (si->myname && si->peername) {
		swrap_dump_packet(si, NULL, SWRAP_CLOSE_RECV, NULL, 0);
		swrap_dump_packet(si, NULL, SWRAP_CLOSE_ACK, NULL, 0);
	}

	if (si->path) free(si->path);
	if (si->myname) free(si->myname);
	if (si->peername) free(si->peername);
	if (si->tmp_path) {
		unlink(si->tmp_path);
		free(si->tmp_path);
	}
	free(si);

	return ret;
}

static int
dup_internal(const struct socket_info *si_oldd, int fd)
{
	struct socket_info *si_newd;

	si_newd = (struct socket_info *)calloc(1, sizeof(struct socket_info));

	si_newd->fd = fd;

	si_newd->family = si_oldd->family;
	si_newd->type = si_oldd->type;
	si_newd->protocol = si_oldd->protocol;
	si_newd->bound = si_oldd->bound;
	si_newd->bcast = si_oldd->bcast;
	if (si_oldd->path)
		si_newd->path = strdup(si_oldd->path);
	if (si_oldd->tmp_path)
		si_newd->tmp_path = strdup(si_oldd->tmp_path);
	si_newd->myname =
	    sockaddr_dup(si_oldd->myname, si_oldd->myname_len);
	si_newd->myname_len = si_oldd->myname_len;
	si_newd->peername =
	    sockaddr_dup(si_oldd->peername, si_oldd->peername_len);
	si_newd->peername_len = si_oldd->peername_len;

	si_newd->io = si_oldd->io;

	SWRAP_DLIST_ADD(sockets, si_newd);

	return fd;
}


_PUBLIC_ int swrap_dup(int oldd)
{
	struct socket_info *si;
	int fd;

	si = find_socket_info(oldd);
	if (si == NULL)
		return real_dup(oldd);

	fd = real_dup(si->fd);
	if (fd < 0)
		return fd;

	return dup_internal(si, fd);
}


_PUBLIC_ int swrap_dup2(int oldd, int newd)
{
	struct socket_info *si_newd, *si_oldd;
	int fd;

	if (newd == oldd)
	    return newd;

	si_oldd = find_socket_info(oldd);
	si_newd = find_socket_info(newd);

	if (si_oldd == NULL && si_newd == NULL)
		return real_dup2(oldd, newd);

	fd = real_dup2(si_oldd->fd, newd);
	if (fd < 0)
		return fd;

	/* close new socket first */
	if (si_newd)
	       	swrap_close(newd);

	return dup_internal(si_oldd, fd);
}
