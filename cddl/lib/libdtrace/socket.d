/*
 * Copyright (c) 2017 George V. Neville-Neil
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * $FreeBSD$
 *
 * Translators and flags for the socket structure.  FreeBSD specific code.
 */

#pragma D depends_on module kernel

/*
 * Option flags per-socket.
 */
#pragma D binding "1.13" SO_DEBUG
inline int SO_DEBUG =		0x0001;		/* turn on debugging info recording */
#pragma D binding "1.13" SO_ACCEPTCONN
inline int SO_ACCEPTCONN = 	0x0002;		/* socket has had listen() */
#pragma D binding "1.13" SO_REUSEADDR
inline int SO_REUSEADDR =	0x0004;		/* allow local address reuse */
#pragma D binding "1.13" SO_KEEPALIVE
inline int SO_KEEPALIVE =	0x0008;		/* keep connections alive */
#pragma D binding "1.13" SO_DONTROUTE
inline int SO_DONTROUTE =	0x0010;		/* just use interface addresses */
#pragma D binding "1.13" SO_BROADCAST
inline int SO_BROADCAST =	0x0020;		/* permit sending of broadcast msgs */
#pragma D binding "1.13" SO_USELOOPBACK
inline int SO_USELOOPBACK =	0x0040;		/* bypass hardware when possible */
#pragma D binding "1.13" SO_LINGER
inline int SO_LINGER =		0x0080;		/* linger on close if data present */
#pragma D binding "1.13" SO_OOBINLINE
inline int SO_OOBINLINE =	0x0100;		/* leave received OOB data in line */
#pragma D binding "1.13" SO_REUSEPORT
inline int SO_REUSEPORT =	0x0200;		/* allow local address & port reuse */
#pragma D binding "1.13" SO_TIMESTAMP
inline int SO_TIMESTAMP =	0x0400;		/* timestamp received dgram traffic */
#pragma D binding "1.13" SO_NOSIGPIPE
inline int SO_NOSIGPIPE =	0x0800;		/* no SIGPIPE from EPIPE */
#pragma D binding "1.13" SO_ACCEPTFILTER
inline int SO_ACCEPTFILTER =	0x1000;		/* there is an accept filter */
#pragma D binding "1.13" SO_BINTIME
inline int SO_BINTIME =		0x2000;		/* timestamp received dgram traffic */
#pragma D binding "1.13" SO_NO_OFFLOAD
inline int SO_NO_OFFLOAD =	0x4000;		/* socket cannot be offloaded */
#pragma D binding "1.13" SO_NO_DDP
inline int SO_NO_DDP =		0x8000;		/* disable direct data placement */

/*
 * Additional options, not kept in so_options.
 */
#pragma D binding "1.13" SO_SNDBUF
inline int SO_SNDBUF =		0x1001;		/* send buffer size */
#pragma D binding "1.13" SO_RCVBUF
inline int SO_RCVBUF =		0x1002;		/* receive buffer size */
#pragma D binding "1.13" SO_SNDLOWAT
inline int SO_SNDLOWAT =	0x1003;		/* send low-water mark */
#pragma D binding "1.13" SO_RCVLOWAT
inline int SO_RCVLOWAT =	0x1004;		/* receive low-water mark */
#pragma D binding "1.13" SO_SNDTIMEO
inline int SO_SNDTIMEO =	0x1005;		/* send timeout */
#pragma D binding "1.13" SO_RCVTIMEO
inline int SO_RCVTIMEO =	0x1006;		/* receive timeout */
#pragma D binding "1.13" SO_ERROR
inline int SO_ERROR =		0x1007;		/* get error status and clear */
#pragma D binding "1.13" SO_TYPE
inline int SO_TYPE =		0x1008;		/* get socket type */
#pragma D binding "1.13" SO_LABEL
inline int SO_LABEL =		0x1009;		/* socket's MAC label */
#pragma D binding "1.13" SO_PEERLABEL
inline int SO_PEERLABEL =	0x1010;		/* socket's peer's MAC label */
#pragma D binding "1.13" SO_LISTENQLIMIT
inline int SO_LISTENQLIMIT =	0x1011;		/* socket's backlog limit */
#pragma D binding "1.13" SO_LISTENQLEN
inline int SO_LISTENQLEN =	0x1012;		/* socket's complete queue length */
#pragma D binding "1.13" SO_LISTENINCQLEN
inline int SO_LISTENINCQLEN =	0x1013;		/* socket's incomplete queue length */
#pragma D binding "1.13" SO_SETFIB
inline int SO_SETFIB =		0x1014;		/* use this FIB to route */
#pragma D binding "1.13" SO_USER_COOKIE
inline int SO_USER_COOKIE =	0x1015;		/* user cookie (dummynet etc.) */
#pragma D binding "1.13" SO_PROTOCOL
inline int SO_PROTOCOL =	0x1016;		/* get socket protocol (Linux name) */
#pragma D binding "1.13" SO_PROTOTYPE
inline int SO_PROTOTYPE =	SO_PROTOCOL;	/* alias for SO_PROTOCOL (SunOS name) */
#pragma D binding "1.13" SO_TS_CLOCK
inline int SO_TS_CLOCK =	0x1017;		/* clock type used for SO_TIMESTAMP */
#pragma D binding "1.13" SO_MAX_PACING_RATE
inline int SO_MAX_PACING_RATE = 0x1018;	/* socket's max TX pacing rate (Linux name) */

#pragma D binding "1.13" SO_TS_REALTIME_MICRO
inline int SO_TS_REALTIME_MICRO =	0;	/* microsecond resolution, realtime */
#pragma D binding "1.13" SO_TS_BINTIME
inline int SO_TS_BINTIME = 		1;	/* sub-nanosecond resolution, realtime */
#pragma D binding "1.13" SO_TS_REALTIME
inline int SO_TS_REALTIME = 		2;	/* nanosecond resolution, realtime */
#pragma D binding "1.13" SO_TS_MONOTONIC
inline int SO_TS_MONOTONIC =		3;	/* nanosecond resolution, monotonic */
#pragma D binding "1.13" SO_TS_DEFAULT
inline int SO_TS_DEFAULT = 		SO_TS_REALTIME_MICRO;
#pragma D binding "1.13" SO_TS_CLOCK_MAX
inline int SO_TS_CLOCK_MAX = 		SO_TS_MONOTONIC;

#pragma D binding "1.13" AF_UNSPEC
inline int AF_UNSPEC =		0;		/* unspecified */
#pragma D binding "1.13" AF_UNIX
inline int AF_UNIX =		1;		/* standardized name for AF_LOCAL */
#pragma D binding "1.13" AF_LOCAL
inline int AF_LOCAL =		AF_UNIX;	/* local to host (pipes, portals) */
#pragma D binding "1.13" AF_INET
inline int AF_INET =		2;		/* internetwork: UDP, TCP, etc. */
#pragma D binding "1.13" AF_IMPLINK
inline int AF_IMPLINK =	3;		/* arpanet imp addresses */
#pragma D binding "1.13" AF_PUP
inline int AF_PUP =		4;		/* pup protocols: e.g. BSP */
#pragma D binding "1.13" AF_CHAOS
inline int AF_CHAOS =		5;		/* mit CHAOS protocols */
#pragma D binding "1.13" AF_NETBIOS
inline int AF_NETBIOS =	6;		/* SMB protocols */
#pragma D binding "1.13" AF_ISO
inline int AF_ISO =		7;		/* ISO protocols */
#pragma D binding "1.13" AF_OSI
inline int AF_OSI =		AF_ISO;
#pragma D binding "1.13" AF_ECMA
inline int AF_ECMA =		8;		/* European computer manufacturers */
#pragma D binding "1.13" AF_DATAKIT
inline int AF_DATAKIT =		9;		/* datakit protocols */
#pragma D binding "1.13" AF_CCITT
inline int AF_CCITT =		10;		/* CCITT protocols, X.25 etc */
#pragma D binding "1.13" AF_SNA
inline int AF_SNA =		11;		/* IBM SNA */
#pragma D binding "1.13" AF_DECnet
inline int AF_DECnet =		12;		/* DECnet */
#pragma D binding "1.13" AF_DLI
inline int AF_DLI =		13;		/* DEC Direct data link interface */
#pragma D binding "1.13" AF_LAT
inline int AF_LAT =		14;		/* LAT */
#pragma D binding "1.13" AF_HYLINK
inline int AF_HYLINK =		15;		/* NSC Hyperchannel */
#pragma D binding "1.13" AF_APPLETALK
inline int AF_APPLETALK =	16;		/* Apple Talk */
#pragma D binding "1.13" AF_ROUTE
inline int AF_ROUTE =		17;		/* Internal Routing Protocol */
#pragma D binding "1.13" AF_LINK
inline int AF_LINK =		18;		/* Link layer interface */
#pragma D binding "1.13" pseudo_AF_XTP
inline int pseudo_AF_XTP =	19;		/* eXpress Transfer Protocol (no AF) */
#pragma D binding "1.13" AF_COIP
inline int AF_COIP =		20;		/* connection-oriented IP, aka ST II */
#pragma D binding "1.13" AF_CNT
inline int AF_CNT =		21;		/* Computer Network Technology */
#pragma D binding "1.13" pseudo_AF_RTIP
inline int pseudo_AF_RTIP =	22;		/* Help Identify RTIP packets */
#pragma D binding "1.13" AF_IPX
inline int AF_IPX =		23;		/* Novell Internet Protocol */
#pragma D binding "1.13" AF_SIP
inline int AF_SIP =		24;		/* Simple Internet Protocol */
#pragma D binding "1.13" pseudo_AF_PIP
inline int pseudo_AF_PIP =	25;		/* Help Identify PIP packets */
#pragma D binding "1.13" AF_ISDN
inline int AF_ISDN =		26;		/* Integrated Services Digital Network*/
#pragma D binding "1.13" AF_E164
inline int AF_E164 =		AF_ISDN;	/* CCITT E.164 recommendation */
#pragma D binding "1.13" pseudo_AF_KEY
inline int pseudo_AF_KEY =	27;		/* Internal key-management function */
#pragma D binding "1.13" AF_INET6
inline int AF_INET6 =		28;		/* IPv6 */
#pragma D binding "1.13" AF_NATM
inline int AF_NATM =		29;		/* native ATM access */
#pragma D binding "1.13" AF_ATM
inline int AF_ATM =		30;		/* ATM */
#pragma D binding "1.13" pseudo_AF_HDRCMPLT
inline int pseudo_AF_HDRCMPLT = 31;	/* Used by BPF to not rewrite headers
					 * in interface output routine
					 */
#pragma D binding "1.13" AF_NETGRAPH
inline int AF_NETGRAPH =	32;		/* Netgraph sockets */
#pragma D binding "1.13" AF_SLOW
inline int AF_SLOW =		33;		/* 802.3ad slow protocol */
#pragma D binding "1.13" AF_SCLUSTER
inline int AF_SCLUSTER =	34;		/* Sitara cluster protocol */
#pragma D binding "1.13" AF_ARP
inline int AF_ARP =		35;		/* Address Resolution Protocol */
#pragma D binding "1.13" AF_BLUETOOTH
inline int AF_BLUETOOTH =	36;		/* Bluetooth sockets */
#pragma D binding "1.13" AF_IEEE80211
inline int AF_IEEE80211 =	37;		/* IEEE 802.11 protocol */
#pragma D binding "1.13" AF_INET_SDP
inline int AF_INET_SDP	=	40;		/* OFED Socket Direct Protocol ipv4 */
#pragma D binding "1.13" AF_INET6_SDP
inline int AF_INET6_SDP =	42;		/* OFED Socket Direct Protocol ipv6 */
#pragma D binding "1.13" AF_MAX
inline int AF_MAX =		42;

/*
 * Protocol families, same as address families for now.
 */
#pragma D binding "1.13" PF_UNSPEC
inline int PF_UNSPEC =	AF_UNSPEC;
#pragma D binding "1.13" PF_LOCAL
inline int PF_LOCAL =	AF_LOCAL;
#pragma D binding "1.13" PF_UNIX
inline int PF_UNIX =	PF_LOCAL;	/* backward compatibility */
#pragma D binding "1.13" PF_INET
inline int PF_INET =	AF_INET;
#pragma D binding "1.13" PF_IMPLINK
inline int PF_IMPLINK =	AF_IMPLINK;
#pragma D binding "1.13" PF_PUP
inline int PF_PUP =	AF_PUP;
#pragma D binding "1.13" PF_CHAOS
inline int PF_CHAOS =	AF_CHAOS;
#pragma D binding "1.13" PF_NETBIOS
inline int PF_NETBIOS =	AF_NETBIOS;
#pragma D binding "1.13" PF_ISO
inline int PF_ISO =	AF_ISO;
#pragma D binding "1.13" PF_OSI
inline int PF_OSI =	AF_ISO;
#pragma D binding "1.13" PF_ECMA
inline int PF_ECMA =	AF_ECMA;
#pragma D binding "1.13" PF_DATAKIT
inline int PF_DATAKIT =	AF_DATAKIT;
#pragma D binding "1.13" PF_CCITT
inline int PF_CCITT =	AF_CCITT;
#pragma D binding "1.13" PF_SNA
inline int PF_SNA =	AF_SNA;
#pragma D binding "1.13" PF_DECnet
inline int PF_DECnet =	AF_DECnet;
#pragma D binding "1.13" PF_DLI
inline int PF_DLI =	AF_DLI;
#pragma D binding "1.13" PF_LAT
inline int PF_LAT =	AF_LAT;
#pragma D binding "1.13" PF_HYLINK
inline int PF_HYLINK =	AF_HYLINK;
#pragma D binding "1.13" PF_APPLETALK
inline int PF_APPLETALK =	AF_APPLETALK;
#pragma D binding "1.13" PF_ROUTE
inline int PF_ROUTE =	AF_ROUTE;
#pragma D binding "1.13" PF_LINK
inline int PF_LINK =	AF_LINK;
#pragma D binding "1.13" PF_XTP
inline int PF_XTP =	pseudo_AF_XTP;	/* really just proto family, no AF */
#pragma D binding "1.13" PF_COIP
inline int PF_COIP =	AF_COIP;
#pragma D binding "1.13" PF_CNT
inline int PF_CNT =	AF_CNT;
#pragma D binding "1.13" PF_SIP
inline int PF_SIP =	AF_SIP;
#pragma D binding "1.13" PF_IPX
inline int PF_IPX =	AF_IPX;
#pragma D binding "1.13" PF_RTIP
inline int PF_RTIP =	pseudo_AF_RTIP;	/* same format as AF_INET */
#pragma D binding "1.13" PF_PIP
inline int PF_PIP =	pseudo_AF_PIP;
#pragma D binding "1.13" PF_ISDN
inline int PF_ISDN =	AF_ISDN;
#pragma D binding "1.13" PF_KEY
inline int PF_KEY =	pseudo_AF_KEY;
#pragma D binding "1.13" PF_INET6
inline int PF_INET6 =	AF_INET6;
#pragma D binding "1.13" PF_NATM
inline int PF_NATM =	AF_NATM;
#pragma D binding "1.13" PF_ATM
inline int PF_ATM =	AF_ATM;
#pragma D binding "1.13" PF_NETGRAPH
inline int PF_NETGRAPH =	AF_NETGRAPH;
#pragma D binding "1.13" PF_SLOW
inline int PF_SLOW =	AF_SLOW;
#pragma D binding "1.13" PF_SCLUSTER
inline int PF_SCLUSTER =	AF_SCLUSTER;
#pragma D binding "1.13" PF_ARP
inline int PF_ARP =	AF_ARP;
#pragma D binding "1.13" PF_BLUETOOTH
inline int PF_BLUETOOTH =	AF_BLUETOOTH;
#pragma D binding "1.13" PF_IEEE80211
inline int PF_IEEE80211 =	AF_IEEE80211;
#pragma D binding "1.13" PF_INET_SDP
inline int PF_INET_SDP=	AF_INET_SDP;
#pragma D binding "1.13" PF_INET6_SDP
inline int PF_INET6_SDP=	AF_INET6_SDP;
#pragma D binding "1.13" PF_MAX
inline int PF_MAX =	AF_MAX;
