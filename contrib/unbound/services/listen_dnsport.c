/*
 * services/listen_dnsport.c - listen on port 53 for incoming DNS queries.
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
 * This file has functions to get queries from clients.
 */
#include "config.h"
#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#include <sys/time.h>
#ifdef USE_TCP_FASTOPEN
#include <netinet/tcp.h>
#endif
#include "services/listen_dnsport.h"
#include "services/outside_network.h"
#include "util/netevent.h"
#include "util/log.h"
#include "util/config_file.h"
#include "util/net_help.h"
#include "sldns/sbuffer.h"

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#include <fcntl.h>

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif

#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

/** number of queued TCP connections for listen() */
#define TCP_BACKLOG 256 

/**
 * Debug print of the getaddrinfo returned address.
 * @param addr: the address returned.
 */
static void
verbose_print_addr(struct addrinfo *addr)
{
	if(verbosity >= VERB_ALGO) {
		char buf[100];
		void* sinaddr = &((struct sockaddr_in*)addr->ai_addr)->sin_addr;
#ifdef INET6
		if(addr->ai_family == AF_INET6)
			sinaddr = &((struct sockaddr_in6*)addr->ai_addr)->
				sin6_addr;
#endif /* INET6 */
		if(inet_ntop(addr->ai_family, sinaddr, buf,
			(socklen_t)sizeof(buf)) == 0) {
			(void)strlcpy(buf, "(null)", sizeof(buf));
		}
		buf[sizeof(buf)-1] = 0;
		verbose(VERB_ALGO, "creating %s%s socket %s %d", 
			addr->ai_socktype==SOCK_DGRAM?"udp":
			addr->ai_socktype==SOCK_STREAM?"tcp":"otherproto",
			addr->ai_family==AF_INET?"4":
			addr->ai_family==AF_INET6?"6":
			"_otherfam", buf, 
			ntohs(((struct sockaddr_in*)addr->ai_addr)->sin_port));
	}
}

#ifdef HAVE_SYSTEMD
static int
systemd_get_activated(int family, int socktype, int listen,
		      struct sockaddr *addr, socklen_t addrlen,
		      const char *path)
{
	int i = 0;
	int r = 0;
	int s = -1;
	const char* listen_pid, *listen_fds;

	/* We should use "listen" option only for stream protocols. For UDP it should be -1 */

	if((r = sd_booted()) < 1) {
		if(r == 0)
			log_warn("systemd is not running");
		else
			log_err("systemd sd_booted(): %s", strerror(-r));
		return -1;
	}

	listen_pid = getenv("LISTEN_PID");
	listen_fds = getenv("LISTEN_FDS");

	if (!listen_pid) {
		log_warn("Systemd mandatory ENV variable is not defined: LISTEN_PID");
		return -1;
	}

	if (!listen_fds) {
		log_warn("Systemd mandatory ENV variable is not defined: LISTEN_FDS");
		return -1;
	}

	if((r = sd_listen_fds(0)) < 1) {
		if(r == 0)
			log_warn("systemd: did not return socket, check unit configuration");
		else
			log_err("systemd sd_listen_fds(): %s", strerror(-r));
		return -1;
	}
	
	for(i = 0; i < r; i++) {
		if(sd_is_socket(SD_LISTEN_FDS_START + i, family, socktype, listen)) {
			s = SD_LISTEN_FDS_START + i;
			break;
		}
	}
	if (s == -1) {
		if (addr)
			log_err_addr("systemd sd_listen_fds()",
				     "no such socket",
				     (struct sockaddr_storage *)addr, addrlen);
		else
			log_err("systemd sd_listen_fds(): %s", path);
	}
	return s;
}
#endif

int
create_udp_sock(int family, int socktype, struct sockaddr* addr,
        socklen_t addrlen, int v6only, int* inuse, int* noproto,
	int rcv, int snd, int listen, int* reuseport, int transparent,
	int freebind, int use_systemd)
{
	int s;
#if defined(SO_REUSEADDR) || defined(SO_REUSEPORT) || defined(IPV6_USE_MIN_MTU)  || defined(IP_TRANSPARENT) || defined(IP_BINDANY) || defined(IP_FREEBIND) || defined (SO_BINDANY)
	int on=1;
#endif
#ifdef IPV6_MTU
	int mtu = IPV6_MIN_MTU;
#endif
#if !defined(SO_RCVBUFFORCE) && !defined(SO_RCVBUF)
	(void)rcv;
#endif
#if !defined(SO_SNDBUFFORCE) && !defined(SO_SNDBUF)
	(void)snd;
#endif
#ifndef IPV6_V6ONLY
	(void)v6only;
#endif
#if !defined(IP_TRANSPARENT) && !defined(IP_BINDANY) && !defined(SO_BINDANY)
	(void)transparent;
#endif
#if !defined(IP_FREEBIND)
	(void)freebind;
#endif
#ifdef HAVE_SYSTEMD
	int got_fd_from_systemd = 0;

	if (!use_systemd
	    || (use_systemd
		&& (s = systemd_get_activated(family, socktype, -1, addr,
					      addrlen, NULL)) == -1)) {
#else
	(void)use_systemd;
#endif
	if((s = socket(family, socktype, 0)) == -1) {
		*inuse = 0;
#ifndef USE_WINSOCK
		if(errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT) {
			*noproto = 1;
			return -1;
		}
		log_err("can't create socket: %s", strerror(errno));
#else
		if(WSAGetLastError() == WSAEAFNOSUPPORT || 
			WSAGetLastError() == WSAEPROTONOSUPPORT) {
			*noproto = 1;
			return -1;
		}
		log_err("can't create socket: %s", 
			wsa_strerror(WSAGetLastError()));
#endif
		*noproto = 0;
		return -1;
	}
#ifdef HAVE_SYSTEMD
	} else {
		got_fd_from_systemd = 1;
	}
#endif
	if(listen) {
#ifdef SO_REUSEADDR
		if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void*)&on, 
			(socklen_t)sizeof(on)) < 0) {
#ifndef USE_WINSOCK
			log_err("setsockopt(.. SO_REUSEADDR ..) failed: %s",
				strerror(errno));
			if(errno != ENOSYS) {
				close(s);
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
#else
			log_err("setsockopt(.. SO_REUSEADDR ..) failed: %s",
				wsa_strerror(WSAGetLastError()));
			closesocket(s);
			*noproto = 0;
			*inuse = 0;
			return -1;
#endif
		}
#endif /* SO_REUSEADDR */
#ifdef SO_REUSEPORT
		/* try to set SO_REUSEPORT so that incoming
		 * queries are distributed evenly among the receiving threads.
		 * Each thread must have its own socket bound to the same port,
		 * with SO_REUSEPORT set on each socket.
		 */
		if (reuseport && *reuseport &&
		    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (void*)&on,
			(socklen_t)sizeof(on)) < 0) {
#ifdef ENOPROTOOPT
			if(errno != ENOPROTOOPT || verbosity >= 3)
				log_warn("setsockopt(.. SO_REUSEPORT ..) failed: %s",
					strerror(errno));
#endif
			/* this option is not essential, we can continue */
			*reuseport = 0;
		}
#else
		(void)reuseport;
#endif /* defined(SO_REUSEPORT) */
#ifdef IP_TRANSPARENT
		if (transparent &&
		    setsockopt(s, IPPROTO_IP, IP_TRANSPARENT, (void*)&on,
		    (socklen_t)sizeof(on)) < 0) {
			log_warn("setsockopt(.. IP_TRANSPARENT ..) failed: %s",
			strerror(errno));
		}
#elif defined(IP_BINDANY)
		if (transparent &&
		    setsockopt(s, (family==AF_INET6? IPPROTO_IPV6:IPPROTO_IP),
		    (family == AF_INET6? IPV6_BINDANY:IP_BINDANY),
		    (void*)&on, (socklen_t)sizeof(on)) < 0) {
			log_warn("setsockopt(.. IP%s_BINDANY ..) failed: %s",
			(family==AF_INET6?"V6":""), strerror(errno));
		}
#elif defined(SO_BINDANY)
		if (transparent &&
		    setsockopt(s, SOL_SOCKET, SO_BINDANY, (void*)&on,
		    (socklen_t)sizeof(on)) < 0) {
			log_warn("setsockopt(.. SO_BINDANY ..) failed: %s",
			strerror(errno));
		}
#endif /* IP_TRANSPARENT || IP_BINDANY || SO_BINDANY */
	}
#ifdef IP_FREEBIND
	if(freebind &&
	    setsockopt(s, IPPROTO_IP, IP_FREEBIND, (void*)&on,
	    (socklen_t)sizeof(on)) < 0) {
		log_warn("setsockopt(.. IP_FREEBIND ..) failed: %s",
		strerror(errno));
	}
#endif /* IP_FREEBIND */
	if(rcv) {
#ifdef SO_RCVBUF
		int got;
		socklen_t slen = (socklen_t)sizeof(got);
#  ifdef SO_RCVBUFFORCE
		/* Linux specific: try to use root permission to override
		 * system limits on rcvbuf. The limit is stored in 
		 * /proc/sys/net/core/rmem_max or sysctl net.core.rmem_max */
		if(setsockopt(s, SOL_SOCKET, SO_RCVBUFFORCE, (void*)&rcv, 
			(socklen_t)sizeof(rcv)) < 0) {
			if(errno != EPERM) {
#    ifndef USE_WINSOCK
				log_err("setsockopt(..., SO_RCVBUFFORCE, "
					"...) failed: %s", strerror(errno));
				close(s);
#    else
				log_err("setsockopt(..., SO_RCVBUFFORCE, "
					"...) failed: %s", 
					wsa_strerror(WSAGetLastError()));
				closesocket(s);
#    endif
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
#  endif /* SO_RCVBUFFORCE */
			if(setsockopt(s, SOL_SOCKET, SO_RCVBUF, (void*)&rcv, 
				(socklen_t)sizeof(rcv)) < 0) {
#  ifndef USE_WINSOCK
				log_err("setsockopt(..., SO_RCVBUF, "
					"...) failed: %s", strerror(errno));
				close(s);
#  else
				log_err("setsockopt(..., SO_RCVBUF, "
					"...) failed: %s", 
					wsa_strerror(WSAGetLastError()));
				closesocket(s);
#  endif
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
			/* check if we got the right thing or if system
			 * reduced to some system max.  Warn if so */
			if(getsockopt(s, SOL_SOCKET, SO_RCVBUF, (void*)&got, 
				&slen) >= 0 && got < rcv/2) {
				log_warn("so-rcvbuf %u was not granted. "
					"Got %u. To fix: start with "
					"root permissions(linux) or sysctl "
					"bigger net.core.rmem_max(linux) or "
					"kern.ipc.maxsockbuf(bsd) values.",
					(unsigned)rcv, (unsigned)got);
			}
#  ifdef SO_RCVBUFFORCE
		}
#  endif
#endif /* SO_RCVBUF */
	}
	/* first do RCVBUF as the receive buffer is more important */
	if(snd) {
#ifdef SO_SNDBUF
		int got;
		socklen_t slen = (socklen_t)sizeof(got);
#  ifdef SO_SNDBUFFORCE
		/* Linux specific: try to use root permission to override
		 * system limits on sndbuf. The limit is stored in 
		 * /proc/sys/net/core/wmem_max or sysctl net.core.wmem_max */
		if(setsockopt(s, SOL_SOCKET, SO_SNDBUFFORCE, (void*)&snd, 
			(socklen_t)sizeof(snd)) < 0) {
			if(errno != EPERM) {
#    ifndef USE_WINSOCK
				log_err("setsockopt(..., SO_SNDBUFFORCE, "
					"...) failed: %s", strerror(errno));
				close(s);
#    else
				log_err("setsockopt(..., SO_SNDBUFFORCE, "
					"...) failed: %s", 
					wsa_strerror(WSAGetLastError()));
				closesocket(s);
#    endif
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
#  endif /* SO_SNDBUFFORCE */
			if(setsockopt(s, SOL_SOCKET, SO_SNDBUF, (void*)&snd, 
				(socklen_t)sizeof(snd)) < 0) {
#  ifndef USE_WINSOCK
				log_err("setsockopt(..., SO_SNDBUF, "
					"...) failed: %s", strerror(errno));
				close(s);
#  else
				log_err("setsockopt(..., SO_SNDBUF, "
					"...) failed: %s", 
					wsa_strerror(WSAGetLastError()));
				closesocket(s);
#  endif
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
			/* check if we got the right thing or if system
			 * reduced to some system max.  Warn if so */
			if(getsockopt(s, SOL_SOCKET, SO_SNDBUF, (void*)&got, 
				&slen) >= 0 && got < snd/2) {
				log_warn("so-sndbuf %u was not granted. "
					"Got %u. To fix: start with "
					"root permissions(linux) or sysctl "
					"bigger net.core.wmem_max(linux) or "
					"kern.ipc.maxsockbuf(bsd) values.",
					(unsigned)snd, (unsigned)got);
			}
#  ifdef SO_SNDBUFFORCE
		}
#  endif
#endif /* SO_SNDBUF */
	}
	if(family == AF_INET6) {
# if defined(IPV6_V6ONLY)
		if(v6only) {
			int val=(v6only==2)?0:1;
			if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, 
				(void*)&val, (socklen_t)sizeof(val)) < 0) {
#ifndef USE_WINSOCK
				log_err("setsockopt(..., IPV6_V6ONLY"
					", ...) failed: %s", strerror(errno));
				close(s);
#else
				log_err("setsockopt(..., IPV6_V6ONLY"
					", ...) failed: %s", 
					wsa_strerror(WSAGetLastError()));
				closesocket(s);
#endif
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
		}
# endif
# if defined(IPV6_USE_MIN_MTU)
		/*
		 * There is no fragmentation of IPv6 datagrams
		 * during forwarding in the network. Therefore
		 * we do not send UDP datagrams larger than
		 * the minimum IPv6 MTU of 1280 octets. The
		 * EDNS0 message length can be larger if the
		 * network stack supports IPV6_USE_MIN_MTU.
		 */
		if (setsockopt(s, IPPROTO_IPV6, IPV6_USE_MIN_MTU,
			(void*)&on, (socklen_t)sizeof(on)) < 0) {
#  ifndef USE_WINSOCK
			log_err("setsockopt(..., IPV6_USE_MIN_MTU, "
				"...) failed: %s", strerror(errno));
			close(s);
#  else
			log_err("setsockopt(..., IPV6_USE_MIN_MTU, "
				"...) failed: %s", 
				wsa_strerror(WSAGetLastError()));
			closesocket(s);
#  endif
			*noproto = 0;
			*inuse = 0;
			return -1;
		}
# elif defined(IPV6_MTU)
		/*
		 * On Linux, to send no larger than 1280, the PMTUD is
		 * disabled by default for datagrams anyway, so we set
		 * the MTU to use.
		 */
		if (setsockopt(s, IPPROTO_IPV6, IPV6_MTU,
			(void*)&mtu, (socklen_t)sizeof(mtu)) < 0) {
#  ifndef USE_WINSOCK
			log_err("setsockopt(..., IPV6_MTU, ...) failed: %s", 
				strerror(errno));
			close(s);
#  else
			log_err("setsockopt(..., IPV6_MTU, ...) failed: %s", 
				wsa_strerror(WSAGetLastError()));
			closesocket(s);
#  endif
			*noproto = 0;
			*inuse = 0;
			return -1;
		}
# endif /* IPv6 MTU */
	} else if(family == AF_INET) {
#  if defined(IP_MTU_DISCOVER) && defined(IP_PMTUDISC_DONT)
/* linux 3.15 has IP_PMTUDISC_OMIT, Hannes Frederic Sowa made it so that
 * PMTU information is not accepted, but fragmentation is allowed
 * if and only if the packet size exceeds the outgoing interface MTU
 * (and also uses the interface mtu to determine the size of the packets).
 * So there won't be any EMSGSIZE error.  Against DNS fragmentation attacks.
 * FreeBSD already has same semantics without setting the option. */
		int omit_set = 0;
		int action;
#   if defined(IP_PMTUDISC_OMIT)
		action = IP_PMTUDISC_OMIT;
		if (setsockopt(s, IPPROTO_IP, IP_MTU_DISCOVER, 
			&action, (socklen_t)sizeof(action)) < 0) {

			if (errno != EINVAL) {
				log_err("setsockopt(..., IP_MTU_DISCOVER, IP_PMTUDISC_OMIT...) failed: %s",
					strerror(errno));

#    ifndef USE_WINSOCK
				close(s);
#    else
				closesocket(s);
#    endif
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
		}
		else
		{
		    omit_set = 1;
		}
#   endif
		if (omit_set == 0) {
   			action = IP_PMTUDISC_DONT;
			if (setsockopt(s, IPPROTO_IP, IP_MTU_DISCOVER,
				&action, (socklen_t)sizeof(action)) < 0) {
				log_err("setsockopt(..., IP_MTU_DISCOVER, IP_PMTUDISC_DONT...) failed: %s",
					strerror(errno));
#    ifndef USE_WINSOCK
				close(s);
#    else
				closesocket(s);
#    endif
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
		}
#  elif defined(IP_DONTFRAG)
		int off = 0;
		if (setsockopt(s, IPPROTO_IP, IP_DONTFRAG, 
			&off, (socklen_t)sizeof(off)) < 0) {
			log_err("setsockopt(..., IP_DONTFRAG, ...) failed: %s",
				strerror(errno));
#    ifndef USE_WINSOCK
			close(s);
#    else
			closesocket(s);
#    endif
			*noproto = 0;
			*inuse = 0;
			return -1;
		}
#  endif /* IPv4 MTU */
	}
	if(
#ifdef HAVE_SYSTEMD
		!got_fd_from_systemd &&
#endif
		bind(s, (struct sockaddr*)addr, addrlen) != 0) {
		*noproto = 0;
		*inuse = 0;
#ifndef USE_WINSOCK
#ifdef EADDRINUSE
		*inuse = (errno == EADDRINUSE);
		/* detect freebsd jail with no ipv6 permission */
		if(family==AF_INET6 && errno==EINVAL)
			*noproto = 1;
		else if(errno != EADDRINUSE &&
			!(errno == EACCES && verbosity < 4 && !listen)) {
			log_err_addr("can't bind socket", strerror(errno),
				(struct sockaddr_storage*)addr, addrlen);
		}
#endif /* EADDRINUSE */
		close(s);
#else /* USE_WINSOCK */
		if(WSAGetLastError() != WSAEADDRINUSE &&
			WSAGetLastError() != WSAEADDRNOTAVAIL &&
			!(WSAGetLastError() == WSAEACCES && verbosity < 4 && !listen)) {
			log_err_addr("can't bind socket", 
				wsa_strerror(WSAGetLastError()),
				(struct sockaddr_storage*)addr, addrlen);
		}
		closesocket(s);
#endif /* USE_WINSOCK */
		return -1;
	}
	if(!fd_set_nonblock(s)) {
		*noproto = 0;
		*inuse = 0;
#ifndef USE_WINSOCK
		close(s);
#else
		closesocket(s);
#endif
		return -1;
	}
	return s;
}

int
create_tcp_accept_sock(struct addrinfo *addr, int v6only, int* noproto,
	int* reuseport, int transparent, int mss, int freebind, int use_systemd)
{
	int s;
#if defined(SO_REUSEADDR) || defined(SO_REUSEPORT) || defined(IPV6_V6ONLY) || defined(IP_TRANSPARENT) || defined(IP_BINDANY) || defined(IP_FREEBIND) || defined(SO_BINDANY)
	int on = 1;
#endif
#ifdef HAVE_SYSTEMD
	int got_fd_from_systemd = 0;
#endif
#ifdef USE_TCP_FASTOPEN
	int qlen;
#endif
#if !defined(IP_TRANSPARENT) && !defined(IP_BINDANY) && !defined(SO_BINDANY)
	(void)transparent;
#endif
#if !defined(IP_FREEBIND)
	(void)freebind;
#endif
	verbose_print_addr(addr);
	*noproto = 0;
#ifdef HAVE_SYSTEMD
	if (!use_systemd ||
	    (use_systemd
	     && (s = systemd_get_activated(addr->ai_family, addr->ai_socktype, 1,
					   addr->ai_addr, addr->ai_addrlen,
					   NULL)) == -1)) {
#else
	(void)use_systemd;
#endif
	if((s = socket(addr->ai_family, addr->ai_socktype, 0)) == -1) {
#ifndef USE_WINSOCK
		if(errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT) {
			*noproto = 1;
			return -1;
		}
		log_err("can't create socket: %s", strerror(errno));
#else
		if(WSAGetLastError() == WSAEAFNOSUPPORT ||
			WSAGetLastError() == WSAEPROTONOSUPPORT) {
			*noproto = 1;
			return -1;
		}
		log_err("can't create socket: %s", 
			wsa_strerror(WSAGetLastError()));
#endif
		return -1;
	}
	if (mss > 0) {
#if defined(IPPROTO_TCP) && defined(TCP_MAXSEG)
		if(setsockopt(s, IPPROTO_TCP, TCP_MAXSEG, (void*)&mss,
			(socklen_t)sizeof(mss)) < 0) {
			#ifndef USE_WINSOCK
			log_err(" setsockopt(.. TCP_MAXSEG ..) failed: %s",
				strerror(errno));
			#else
			log_err(" setsockopt(.. TCP_MAXSEG ..) failed: %s",
				wsa_strerror(WSAGetLastError()));
			#endif
		} else {
			verbose(VERB_ALGO,
				" tcp socket mss set to %d", mss);
		}
#else
		log_warn(" setsockopt(TCP_MAXSEG) unsupported");
#endif /* defined(IPPROTO_TCP) && defined(TCP_MAXSEG) */
	}
#ifdef HAVE_SYSTEMD
	} else {
		got_fd_from_systemd = 1;
    }
#endif
#ifdef SO_REUSEADDR
	if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void*)&on, 
		(socklen_t)sizeof(on)) < 0) {
#ifndef USE_WINSOCK
		log_err("setsockopt(.. SO_REUSEADDR ..) failed: %s",
			strerror(errno));
		close(s);
#else
		log_err("setsockopt(.. SO_REUSEADDR ..) failed: %s",
			wsa_strerror(WSAGetLastError()));
		closesocket(s);
#endif
		return -1;
	}
#endif /* SO_REUSEADDR */
#ifdef IP_FREEBIND
	if (freebind && setsockopt(s, IPPROTO_IP, IP_FREEBIND, (void*)&on,
	    (socklen_t)sizeof(on)) < 0) {
		log_warn("setsockopt(.. IP_FREEBIND ..) failed: %s",
		strerror(errno));
	}
#endif /* IP_FREEBIND */
#ifdef SO_REUSEPORT
	/* try to set SO_REUSEPORT so that incoming
	 * connections are distributed evenly among the receiving threads.
	 * Each thread must have its own socket bound to the same port,
	 * with SO_REUSEPORT set on each socket.
	 */
	if (reuseport && *reuseport &&
		setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (void*)&on,
		(socklen_t)sizeof(on)) < 0) {
#ifdef ENOPROTOOPT
		if(errno != ENOPROTOOPT || verbosity >= 3)
			log_warn("setsockopt(.. SO_REUSEPORT ..) failed: %s",
				strerror(errno));
#endif
		/* this option is not essential, we can continue */
		*reuseport = 0;
	}
#else
	(void)reuseport;
#endif /* defined(SO_REUSEPORT) */
#if defined(IPV6_V6ONLY)
	if(addr->ai_family == AF_INET6 && v6only) {
		if(setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, 
			(void*)&on, (socklen_t)sizeof(on)) < 0) {
#ifndef USE_WINSOCK
			log_err("setsockopt(..., IPV6_V6ONLY, ...) failed: %s",
				strerror(errno));
			close(s);
#else
			log_err("setsockopt(..., IPV6_V6ONLY, ...) failed: %s",
				wsa_strerror(WSAGetLastError()));
			closesocket(s);
#endif
			return -1;
		}
	}
#else
	(void)v6only;
#endif /* IPV6_V6ONLY */
#ifdef IP_TRANSPARENT
	if (transparent &&
	    setsockopt(s, IPPROTO_IP, IP_TRANSPARENT, (void*)&on,
	    (socklen_t)sizeof(on)) < 0) {
		log_warn("setsockopt(.. IP_TRANSPARENT ..) failed: %s",
			strerror(errno));
	}
#elif defined(IP_BINDANY)
	if (transparent &&
	    setsockopt(s, (addr->ai_family==AF_INET6? IPPROTO_IPV6:IPPROTO_IP),
	    (addr->ai_family == AF_INET6? IPV6_BINDANY:IP_BINDANY),
	    (void*)&on, (socklen_t)sizeof(on)) < 0) {
		log_warn("setsockopt(.. IP%s_BINDANY ..) failed: %s",
		(addr->ai_family==AF_INET6?"V6":""), strerror(errno));
	}
#elif defined(SO_BINDANY)
	if (transparent &&
	    setsockopt(s, SOL_SOCKET, SO_BINDANY, (void*)&on, (socklen_t)
	    sizeof(on)) < 0) {
		log_warn("setsockopt(.. SO_BINDANY ..) failed: %s",
		strerror(errno));
	}
#endif /* IP_TRANSPARENT || IP_BINDANY || SO_BINDANY */
	if(
#ifdef HAVE_SYSTEMD
		!got_fd_from_systemd &&
#endif
        bind(s, addr->ai_addr, addr->ai_addrlen) != 0) {
#ifndef USE_WINSOCK
		/* detect freebsd jail with no ipv6 permission */
		if(addr->ai_family==AF_INET6 && errno==EINVAL)
			*noproto = 1;
		else {
			log_err_addr("can't bind socket", strerror(errno),
				(struct sockaddr_storage*)addr->ai_addr,
				addr->ai_addrlen);
		}
		close(s);
#else
		log_err_addr("can't bind socket", 
			wsa_strerror(WSAGetLastError()),
			(struct sockaddr_storage*)addr->ai_addr,
			addr->ai_addrlen);
		closesocket(s);
#endif
		return -1;
	}
	if(!fd_set_nonblock(s)) {
#ifndef USE_WINSOCK
		close(s);
#else
		closesocket(s);
#endif
		return -1;
	}
	if(listen(s, TCP_BACKLOG) == -1) {
#ifndef USE_WINSOCK
		log_err("can't listen: %s", strerror(errno));
		close(s);
#else
		log_err("can't listen: %s", wsa_strerror(WSAGetLastError()));
		closesocket(s);
#endif
		return -1;
	}
#ifdef USE_TCP_FASTOPEN
	/* qlen specifies how many outstanding TFO requests to allow. Limit is a defense
	   against IP spoofing attacks as suggested in RFC7413 */
#ifdef __APPLE__
	/* OS X implementation only supports qlen of 1 via this call. Actual
	   value is configured by the net.inet.tcp.fastopen_backlog kernel parm. */
	qlen = 1;
#else
	/* 5 is recommended on linux */
	qlen = 5;
#endif
	if ((setsockopt(s, IPPROTO_TCP, TCP_FASTOPEN, &qlen, 
		  sizeof(qlen))) == -1 ) {
#ifdef ENOPROTOOPT
		/* squelch ENOPROTOOPT: freebsd server mode with kernel support
		   disabled, except when verbosity enabled for debugging */
		if(errno != ENOPROTOOPT || verbosity >= 3)
#endif
		  log_err("Setting TCP Fast Open as server failed: %s", strerror(errno));
	}
#endif
	return s;
}

int
create_local_accept_sock(const char *path, int* noproto, int use_systemd)
{
#ifdef HAVE_SYSTEMD
	int ret;

	if (use_systemd && (ret = systemd_get_activated(AF_LOCAL, SOCK_STREAM, 1, NULL, 0, path)) != -1)
		return ret;
	else {
#endif
#ifdef HAVE_SYS_UN_H
	int s;
	struct sockaddr_un usock;
#ifndef HAVE_SYSTEMD
	(void)use_systemd;
#endif

	verbose(VERB_ALGO, "creating unix socket %s", path);
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
	/* this member exists on BSDs, not Linux */
	usock.sun_len = (unsigned)sizeof(usock);
#endif
	usock.sun_family = AF_LOCAL;
	/* length is 92-108, 104 on FreeBSD */
	(void)strlcpy(usock.sun_path, path, sizeof(usock.sun_path));

	if ((s = socket(AF_LOCAL, SOCK_STREAM, 0)) == -1) {
		log_err("Cannot create local socket %s (%s)",
			path, strerror(errno));
		return -1;
	}

	if (unlink(path) && errno != ENOENT) {
		/* The socket already exists and cannot be removed */
		log_err("Cannot remove old local socket %s (%s)",
			path, strerror(errno));
		goto err;
	}

	if (bind(s, (struct sockaddr *)&usock,
		(socklen_t)sizeof(struct sockaddr_un)) == -1) {
		log_err("Cannot bind local socket %s (%s)",
			path, strerror(errno));
		goto err;
	}

	if (!fd_set_nonblock(s)) {
		log_err("Cannot set non-blocking mode");
		goto err;
	}

	if (listen(s, TCP_BACKLOG) == -1) {
		log_err("can't listen: %s", strerror(errno));
		goto err;
	}

	(void)noproto; /*unused*/
	return s;

err:
#ifndef USE_WINSOCK
	close(s);
#else
	closesocket(s);
#endif
	return -1;

#ifdef HAVE_SYSTEMD
	}
#endif
#else
	(void)use_systemd;
	(void)path;
	log_err("Local sockets are not supported");
	*noproto = 1;
	return -1;
#endif
}


/**
 * Create socket from getaddrinfo results
 */
static int
make_sock(int stype, const char* ifname, const char* port, 
	struct addrinfo *hints, int v6only, int* noip6, size_t rcv, size_t snd,
	int* reuseport, int transparent, int tcp_mss, int freebind, int use_systemd)
{
	struct addrinfo *res = NULL;
	int r, s, inuse, noproto;
	hints->ai_socktype = stype;
	*noip6 = 0;
	if((r=getaddrinfo(ifname, port, hints, &res)) != 0 || !res) {
#ifdef USE_WINSOCK
		if(r == EAI_NONAME && hints->ai_family == AF_INET6){
			*noip6 = 1; /* 'Host not found' for IP6 on winXP */
			return -1;
		}
#endif
		log_err("node %s:%s getaddrinfo: %s %s", 
			ifname?ifname:"default", port, gai_strerror(r),
#ifdef EAI_SYSTEM
			r==EAI_SYSTEM?(char*)strerror(errno):""
#else
			""
#endif
		);
		return -1;
	}
	if(stype == SOCK_DGRAM) {
		verbose_print_addr(res);
		s = create_udp_sock(res->ai_family, res->ai_socktype,
			(struct sockaddr*)res->ai_addr, res->ai_addrlen,
			v6only, &inuse, &noproto, (int)rcv, (int)snd, 1,
			reuseport, transparent, freebind, use_systemd);
		if(s == -1 && inuse) {
			log_err("bind: address already in use");
		} else if(s == -1 && noproto && hints->ai_family == AF_INET6){
			*noip6 = 1;
		}
	} else	{
		s = create_tcp_accept_sock(res, v6only, &noproto, reuseport,
			transparent, tcp_mss, freebind, use_systemd);
		if(s == -1 && noproto && hints->ai_family == AF_INET6){
			*noip6 = 1;
		}
	}
	freeaddrinfo(res);
	return s;
}

/** make socket and first see if ifname contains port override info */
static int
make_sock_port(int stype, const char* ifname, const char* port, 
	struct addrinfo *hints, int v6only, int* noip6, size_t rcv, size_t snd,
	int* reuseport, int transparent, int tcp_mss, int freebind, int use_systemd)
{
	char* s = strchr(ifname, '@');
	if(s) {
		/* override port with ifspec@port */
		char p[16];
		char newif[128];
		if((size_t)(s-ifname) >= sizeof(newif)) {
			log_err("ifname too long: %s", ifname);
			*noip6 = 0;
			return -1;
		}
		if(strlen(s+1) >= sizeof(p)) {
			log_err("portnumber too long: %s", ifname);
			*noip6 = 0;
			return -1;
		}
		(void)strlcpy(newif, ifname, sizeof(newif));
		newif[s-ifname] = 0;
		(void)strlcpy(p, s+1, sizeof(p));
		p[strlen(s+1)]=0;
		return make_sock(stype, newif, p, hints, v6only, noip6,
			rcv, snd, reuseport, transparent, tcp_mss, freebind, use_systemd);
	}
	return make_sock(stype, ifname, port, hints, v6only, noip6, rcv, snd,
		reuseport, transparent, tcp_mss, freebind, use_systemd);
}

/**
 * Add port to open ports list.
 * @param list: list head. changed.
 * @param s: fd.
 * @param ftype: if fd is UDP.
 * @return false on failure. list in unchanged then.
 */
static int
port_insert(struct listen_port** list, int s, enum listen_type ftype)
{
	struct listen_port* item = (struct listen_port*)malloc(
		sizeof(struct listen_port));
	if(!item)
		return 0;
	item->next = *list;
	item->fd = s;
	item->ftype = ftype;
	*list = item;
	return 1;
}

/** set fd to receive source address packet info */
static int
set_recvpktinfo(int s, int family) 
{
#if defined(IPV6_RECVPKTINFO) || defined(IPV6_PKTINFO) || (defined(IP_RECVDSTADDR) && defined(IP_SENDSRCADDR)) || defined(IP_PKTINFO)
	int on = 1;
#else
	(void)s;
#endif
	if(family == AF_INET6) {
#           ifdef IPV6_RECVPKTINFO
		if(setsockopt(s, IPPROTO_IPV6, IPV6_RECVPKTINFO,
			(void*)&on, (socklen_t)sizeof(on)) < 0) {
			log_err("setsockopt(..., IPV6_RECVPKTINFO, ...) failed: %s",
				strerror(errno));
			return 0;
		}
#           elif defined(IPV6_PKTINFO)
		if(setsockopt(s, IPPROTO_IPV6, IPV6_PKTINFO,
			(void*)&on, (socklen_t)sizeof(on)) < 0) {
			log_err("setsockopt(..., IPV6_PKTINFO, ...) failed: %s",
				strerror(errno));
			return 0;
		}
#           else
		log_err("no IPV6_RECVPKTINFO and no IPV6_PKTINFO option, please "
			"disable interface-automatic or do-ip6 in config");
		return 0;
#           endif /* defined IPV6_RECVPKTINFO */

	} else if(family == AF_INET) {
#           ifdef IP_PKTINFO
		if(setsockopt(s, IPPROTO_IP, IP_PKTINFO,
			(void*)&on, (socklen_t)sizeof(on)) < 0) {
			log_err("setsockopt(..., IP_PKTINFO, ...) failed: %s",
				strerror(errno));
			return 0;
		}
#           elif defined(IP_RECVDSTADDR) && defined(IP_SENDSRCADDR)
		if(setsockopt(s, IPPROTO_IP, IP_RECVDSTADDR,
			(void*)&on, (socklen_t)sizeof(on)) < 0) {
			log_err("setsockopt(..., IP_RECVDSTADDR, ...) failed: %s",
				strerror(errno));
			return 0;
		}
#           else
		log_err("no IP_SENDSRCADDR or IP_PKTINFO option, please disable "
			"interface-automatic or do-ip4 in config");
		return 0;
#           endif /* IP_PKTINFO */

	}
	return 1;
}

/** see if interface is ssl, its port number == the ssl port number */
static int
if_is_ssl(const char* ifname, const char* port, int ssl_port,
	struct config_strlist* tls_additional_port)
{
	struct config_strlist* s;
	char* p = strchr(ifname, '@');
	if(!p && atoi(port) == ssl_port)
		return 1;
	if(p && atoi(p+1) == ssl_port)
		return 1;
	for(s = tls_additional_port; s; s = s->next) {
		if(p && atoi(p+1) == atoi(s->str))
			return 1;
		if(!p && atoi(port) == atoi(s->str))
			return 1;
	}
	return 0;
}

/**
 * Helper for ports_open. Creates one interface (or NULL for default).
 * @param ifname: The interface ip address.
 * @param do_auto: use automatic interface detection.
 * 	If enabled, then ifname must be the wildcard name.
 * @param do_udp: if udp should be used.
 * @param do_tcp: if udp should be used.
 * @param hints: for getaddrinfo. family and flags have to be set by caller.
 * @param port: Port number to use (as string).
 * @param list: list of open ports, appended to, changed to point to list head.
 * @param rcv: receive buffer size for UDP
 * @param snd: send buffer size for UDP
 * @param ssl_port: ssl service port number
 * @param tls_additional_port: list of additional ssl service port numbers.
 * @param reuseport: try to set SO_REUSEPORT if nonNULL and true.
 * 	set to false on exit if reuseport failed due to no kernel support.
 * @param transparent: set IP_TRANSPARENT socket option.
 * @param tcp_mss: maximum segment size of tcp socket. default if zero.
 * @param freebind: set IP_FREEBIND socket option.
 * @param use_systemd: if true, fetch sockets from systemd.
 * @param dnscrypt_port: dnscrypt service port number
 * @return: returns false on error.
 */
static int
ports_create_if(const char* ifname, int do_auto, int do_udp, int do_tcp, 
	struct addrinfo *hints, const char* port, struct listen_port** list,
	size_t rcv, size_t snd, int ssl_port,
	struct config_strlist* tls_additional_port, int* reuseport,
	int transparent, int tcp_mss, int freebind, int use_systemd,
	int dnscrypt_port)
{
	int s, noip6=0;
#ifdef USE_DNSCRYPT
	int is_dnscrypt = ((strchr(ifname, '@') && 
			atoi(strchr(ifname, '@')+1) == dnscrypt_port) ||
			(!strchr(ifname, '@') && atoi(port) == dnscrypt_port));
#else
	int is_dnscrypt = 0;
	(void)dnscrypt_port;
#endif

	if(!do_udp && !do_tcp)
		return 0;
	if(do_auto) {
		if((s = make_sock_port(SOCK_DGRAM, ifname, port, hints, 1, 
			&noip6, rcv, snd, reuseport, transparent,
			tcp_mss, freebind, use_systemd)) == -1) {
			if(noip6) {
				log_warn("IPv6 protocol not available");
				return 1;
			}
			return 0;
		}
		/* getting source addr packet info is highly non-portable */
		if(!set_recvpktinfo(s, hints->ai_family)) {
#ifndef USE_WINSOCK
			close(s);
#else
			closesocket(s);
#endif
			return 0;
		}
		if(!port_insert(list, s,
		   is_dnscrypt?listen_type_udpancil_dnscrypt:listen_type_udpancil)) {
#ifndef USE_WINSOCK
			close(s);
#else
			closesocket(s);
#endif
			return 0;
		}
	} else if(do_udp) {
		/* regular udp socket */
		if((s = make_sock_port(SOCK_DGRAM, ifname, port, hints, 1, 
			&noip6, rcv, snd, reuseport, transparent,
			tcp_mss, freebind, use_systemd)) == -1) {
			if(noip6) {
				log_warn("IPv6 protocol not available");
				return 1;
			}
			return 0;
		}
		if(!port_insert(list, s,
		   is_dnscrypt?listen_type_udp_dnscrypt:listen_type_udp)) {
#ifndef USE_WINSOCK
			close(s);
#else
			closesocket(s);
#endif
			return 0;
		}
	}
	if(do_tcp) {
		int is_ssl = if_is_ssl(ifname, port, ssl_port,
			tls_additional_port);
		if((s = make_sock_port(SOCK_STREAM, ifname, port, hints, 1, 
			&noip6, 0, 0, reuseport, transparent, tcp_mss,
			freebind, use_systemd)) == -1) {
			if(noip6) {
				/*log_warn("IPv6 protocol not available");*/
				return 1;
			}
			return 0;
		}
		if(is_ssl)
			verbose(VERB_ALGO, "setup TCP for SSL service");
		if(!port_insert(list, s, is_ssl?listen_type_ssl:
			(is_dnscrypt?listen_type_tcp_dnscrypt:listen_type_tcp))) {
#ifndef USE_WINSOCK
			close(s);
#else
			closesocket(s);
#endif
			return 0;
		}
	}
	return 1;
}

/** 
 * Add items to commpoint list in front.
 * @param c: commpoint to add.
 * @param front: listen struct.
 * @return: false on failure.
 */
static int
listen_cp_insert(struct comm_point* c, struct listen_dnsport* front)
{
	struct listen_list* item = (struct listen_list*)malloc(
		sizeof(struct listen_list));
	if(!item)
		return 0;
	item->com = c;
	item->next = front->cps;
	front->cps = item;
	return 1;
}

struct listen_dnsport* 
listen_create(struct comm_base* base, struct listen_port* ports,
	size_t bufsize, int tcp_accept_count, int tcp_idle_timeout,
	struct tcl_list* tcp_conn_limit, void* sslctx,
	struct dt_env* dtenv, comm_point_callback_type* cb, void *cb_arg)
{
	struct listen_dnsport* front = (struct listen_dnsport*)
		malloc(sizeof(struct listen_dnsport));
	if(!front)
		return NULL;
	front->cps = NULL;
	front->udp_buff = sldns_buffer_new(bufsize);
#ifdef USE_DNSCRYPT
	front->dnscrypt_udp_buff = NULL;
#endif
	if(!front->udp_buff) {
		free(front);
		return NULL;
	}

	/* create comm points as needed */
	while(ports) {
		struct comm_point* cp = NULL;
		if(ports->ftype == listen_type_udp ||
		   ports->ftype == listen_type_udp_dnscrypt)
			cp = comm_point_create_udp(base, ports->fd, 
				front->udp_buff, cb, cb_arg);
		else if(ports->ftype == listen_type_tcp ||
				ports->ftype == listen_type_tcp_dnscrypt)
			cp = comm_point_create_tcp(base, ports->fd, 
				tcp_accept_count, tcp_idle_timeout,
				tcp_conn_limit, bufsize, cb, cb_arg);
		else if(ports->ftype == listen_type_ssl) {
			cp = comm_point_create_tcp(base, ports->fd, 
				tcp_accept_count, tcp_idle_timeout,
				tcp_conn_limit, bufsize, cb, cb_arg);
			cp->ssl = sslctx;
		} else if(ports->ftype == listen_type_udpancil ||
				  ports->ftype == listen_type_udpancil_dnscrypt)
			cp = comm_point_create_udp_ancil(base, ports->fd, 
				front->udp_buff, cb, cb_arg);
		if(!cp) {
			log_err("can't create commpoint");	
			listen_delete(front);
			return NULL;
		}
		cp->dtenv = dtenv;
		cp->do_not_close = 1;
#ifdef USE_DNSCRYPT
		if (ports->ftype == listen_type_udp_dnscrypt ||
			ports->ftype == listen_type_tcp_dnscrypt ||
			ports->ftype == listen_type_udpancil_dnscrypt) {
			cp->dnscrypt = 1;
			cp->dnscrypt_buffer = sldns_buffer_new(bufsize);
			if(!cp->dnscrypt_buffer) {
				log_err("can't alloc dnscrypt_buffer");
				comm_point_delete(cp);
				listen_delete(front);
				return NULL;
			}
			front->dnscrypt_udp_buff = cp->dnscrypt_buffer;
		}
#endif
		if(!listen_cp_insert(cp, front)) {
			log_err("malloc failed");
			comm_point_delete(cp);
			listen_delete(front);
			return NULL;
		}
		ports = ports->next;
	}
	if(!front->cps) {
		log_err("Could not open sockets to accept queries.");
		listen_delete(front);
		return NULL;
	}

	return front;
}

void
listen_list_delete(struct listen_list* list)
{
	struct listen_list *p = list, *pn;
	while(p) {
		pn = p->next;
		comm_point_delete(p->com);
		free(p);
		p = pn;
	}
}

void 
listen_delete(struct listen_dnsport* front)
{
	if(!front) 
		return;
	listen_list_delete(front->cps);
#ifdef USE_DNSCRYPT
	if(front->dnscrypt_udp_buff &&
		front->udp_buff != front->dnscrypt_udp_buff) {
		sldns_buffer_free(front->dnscrypt_udp_buff);
	}
#endif
	sldns_buffer_free(front->udp_buff);
	free(front);
}

struct listen_port* 
listening_ports_open(struct config_file* cfg, int* reuseport)
{
	struct listen_port* list = NULL;
	struct addrinfo hints;
	int i, do_ip4, do_ip6;
	int do_tcp, do_auto;
	char portbuf[32];
	snprintf(portbuf, sizeof(portbuf), "%d", cfg->port);
	do_ip4 = cfg->do_ip4;
	do_ip6 = cfg->do_ip6;
	do_tcp = cfg->do_tcp;
	do_auto = cfg->if_automatic && cfg->do_udp;
	if(cfg->incoming_num_tcp == 0)
		do_tcp = 0;

	/* getaddrinfo */
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE;
	/* no name lookups on our listening ports */
	if(cfg->num_ifs > 0)
		hints.ai_flags |= AI_NUMERICHOST;
	hints.ai_family = AF_UNSPEC;
#ifndef INET6
	do_ip6 = 0;
#endif
	if(!do_ip4 && !do_ip6) {
		return NULL;
	}
	/* create ip4 and ip6 ports so that return addresses are nice. */
	if(do_auto || cfg->num_ifs == 0) {
		if(do_ip6) {
			hints.ai_family = AF_INET6;
			if(!ports_create_if(do_auto?"::0":"::1", 
				do_auto, cfg->do_udp, do_tcp, 
				&hints, portbuf, &list,
				cfg->so_rcvbuf, cfg->so_sndbuf,
				cfg->ssl_port, cfg->tls_additional_port,
				reuseport, cfg->ip_transparent,
				cfg->tcp_mss, cfg->ip_freebind, cfg->use_systemd,
				cfg->dnscrypt_port)) {
				listening_ports_free(list);
				return NULL;
			}
		}
		if(do_ip4) {
			hints.ai_family = AF_INET;
			if(!ports_create_if(do_auto?"0.0.0.0":"127.0.0.1", 
				do_auto, cfg->do_udp, do_tcp, 
				&hints, portbuf, &list,
				cfg->so_rcvbuf, cfg->so_sndbuf,
				cfg->ssl_port, cfg->tls_additional_port,
				reuseport, cfg->ip_transparent,
				cfg->tcp_mss, cfg->ip_freebind, cfg->use_systemd,
				cfg->dnscrypt_port)) {
				listening_ports_free(list);
				return NULL;
			}
		}
	} else for(i = 0; i<cfg->num_ifs; i++) {
		if(str_is_ip6(cfg->ifs[i])) {
			if(!do_ip6)
				continue;
			hints.ai_family = AF_INET6;
			if(!ports_create_if(cfg->ifs[i], 0, cfg->do_udp, 
				do_tcp, &hints, portbuf, &list, 
				cfg->so_rcvbuf, cfg->so_sndbuf,
				cfg->ssl_port, cfg->tls_additional_port,
				reuseport, cfg->ip_transparent,
				cfg->tcp_mss, cfg->ip_freebind, cfg->use_systemd,
				cfg->dnscrypt_port)) {
				listening_ports_free(list);
				return NULL;
			}
		} else {
			if(!do_ip4)
				continue;
			hints.ai_family = AF_INET;
			if(!ports_create_if(cfg->ifs[i], 0, cfg->do_udp, 
				do_tcp, &hints, portbuf, &list, 
				cfg->so_rcvbuf, cfg->so_sndbuf,
				cfg->ssl_port, cfg->tls_additional_port,
				reuseport, cfg->ip_transparent,
				cfg->tcp_mss, cfg->ip_freebind, cfg->use_systemd,
				cfg->dnscrypt_port)) {
				listening_ports_free(list);
				return NULL;
			}
		}
	}
	return list;
}

void listening_ports_free(struct listen_port* list)
{
	struct listen_port* nx;
	while(list) {
		nx = list->next;
		if(list->fd != -1) {
#ifndef USE_WINSOCK
			close(list->fd);
#else
			closesocket(list->fd);
#endif
		}
		free(list);
		list = nx;
	}
}

size_t listen_get_mem(struct listen_dnsport* listen)
{
	struct listen_list* p;
	size_t s = sizeof(*listen) + sizeof(*listen->base) + 
		sizeof(*listen->udp_buff) + 
		sldns_buffer_capacity(listen->udp_buff);
#ifdef USE_DNSCRYPT
	s += sizeof(*listen->dnscrypt_udp_buff);
	if(listen->udp_buff != listen->dnscrypt_udp_buff){
		s += sldns_buffer_capacity(listen->dnscrypt_udp_buff);
	}
#endif
	for(p = listen->cps; p; p = p->next) {
		s += sizeof(*p);
		s += comm_point_get_mem(p->com);
	}
	return s;
}

void listen_stop_accept(struct listen_dnsport* listen)
{
	/* do not stop the ones that have no tcp_free list
	 * (they have already stopped listening) */
	struct listen_list* p;
	for(p=listen->cps; p; p=p->next) {
		if(p->com->type == comm_tcp_accept &&
			p->com->tcp_free != NULL) {
			comm_point_stop_listening(p->com);
		}
	}
}

void listen_start_accept(struct listen_dnsport* listen)
{
	/* do not start the ones that have no tcp_free list, it is no
	 * use to listen to them because they have no free tcp handlers */
	struct listen_list* p;
	for(p=listen->cps; p; p=p->next) {
		if(p->com->type == comm_tcp_accept &&
			p->com->tcp_free != NULL) {
			comm_point_start_listening(p->com, -1, -1);
		}
	}
}

