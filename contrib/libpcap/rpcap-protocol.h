/*
 * Copyright (c) 2002 - 2005 NetGroup, Politecnico di Torino (Italy)
 * Copyright (c) 2005 - 2008 CACE Technologies, Davis (California)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Politecnico di Torino, CACE Technologies
 * nor the names of its contributors may be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef __RPCAP_PROTOCOL_H__
#define __RPCAP_PROTOCOL_H__

#define RPCAP_DEFAULT_NETPORT "2002" /* Default port on which the RPCAP daemon is waiting for connections. */
/* Default port on which the client workstation is waiting for connections in case of active mode. */
#define RPCAP_DEFAULT_NETPORT_ACTIVE "2003"
#define RPCAP_DEFAULT_NETADDR ""	/* Default network address on which the RPCAP daemon binds to. */

/*
 * Minimum and maximum supported versions of the protocol.
 *
 * If new message types are added, the protocol version MUST be changed,
 * so that a client knows, from the negotiated protocol version, what
 * messages can be sent to the server.
 *
 * If the format of an existing message type is changed, the protocol
 * version MUST be changed, so that each side knows, from the negotiated
 * protocol version, what format should be used.
 *
 * The RPCAP_MSG_ERROR format MUST not change, as it's used to, among
 * other things, report "incorrect version number" errors, where, if
 * the format changed, the sender of the message might not know what
 * versions the recipient would understand, or might know a version
 * they support (the version number they sent) but might not know
 * the format of the message in that version.
 *
 * Other message versions SHOULD not change, as that would complicate
 * the process of interpreting the message, making it version-dependent.
 * Introducing a new message with a new format is preferable.
 *
 * Version negotiation is done as part of the authentication process:
 *
 * The client sends an authentication request, with the version number
 * in the request being the maximum version it supports.
 *
 * If the server supports that version, it attempts to authenticate the
 * client, and replies as appropriate, with the version number in the
 * reply being that version.
 *
 * If the server doesn't support that version because it's too large,
 * it replies with a RPCAP_MSG_ERROR message, with the maximum version
 * they support as the version number in the reply, and with the error
 * code being PCAP_ERR_WRONGVER.
 *
 * If the server doesn't support that version because it's too small,
 * it replies with a RPCAP_MSG_ERROR message, with that version as
 * the version number in the reply, and with the error code being
 * PCAP_ERR_WRONGVER.
 *
 * If the client supports that version, it retries the authentication
 * with that version and, if that fails for any reason, including
 * PCAP_ERR_WRONGVER, fails.  Otherwise, it fails, telling its caller
 * that there's no version that both support.
 *
 * This requires that the set of versions supported by a client or
 * server be a range of integers, with no gaps.  Thus:
 *
 * the client's version set is [Cmin, Cmax], with Cmin <= Cmax;
 *
 * the server's version set is [Smin, Smax], with Smin <= Smax;
 *
 * the client sends Cmax as the version number in the initial
 * authentication request;
 *
 * if the server doesn't support the version sent by the client,
 * either Smax < Cmax or Smin > Cmax (because the client sent Cmax
 * to the server, and the server doesn't support it);
 *
 * if Smax < Cmax:
 *
 *    the server sends Smax as the version number in the RPCAP_MSG_ERROR/
 *    PCAP_ERR_WRONGVER message - the client will accept this because
 *    Cmax != 0, as these numbers are unsigned, and this means that
 *    this isn't an old client that rejects all messages with a non-zero
 *    version number, it's a new client that accepts RPCAP_MSG_ERROR
 *    messages no matter what the version is;
 *
 *    if Smax >= Cmin, both the client and the server can use it, and
 *    the client retries with Smax;
 *
 *    if Smax < Cmin, there is no version the client and server can
 *    both support.
 *
 * if Smin > Cmax:
 *
 *    the server sends Cmax as the version number in the RPCAP_MSG_ERROR/
 *    PCAP_ERR_WRONGVER message - the client will accept this because
 *    Cmax is a valid client version number.
 *
 *    the client will retry with Cmax, get the same version failure,
 *    and report that there is no version the client and server can
 *    both support (as the version sets are disjoint).
 *
 * Old negotiation-unaware clients just send version 0 and, if they
 * get back PCAP_ERR_WRONGVER, treat it as a fatal error.  This
 * means they'll fail to talk to any server that can't handle
 * version 0, which is the appropriate thing to do, as they can
 * only use version 0.
 *
 * Old negotiation-unaware servers fail if they get a version other
 * than 0, sending back PCAP_ERR_WRONGVER with version 0, which is
 * the only version, and thus both the minimum and maximum version,
 * they support.  The client will either fail if it doesn't support
 * version 0, or will retry with version 0 and succeed, so it will
 * fail with servers that can't handle version 0 or will negotiate
 * version 0 with servers that can handle version 0.
 */
#define RPCAP_MIN_VERSION 0
#define RPCAP_MAX_VERSION 0

/*
 * Version numbers are unsigned, so if RPCAP_MIN_VERSION is 0, they
 * are >= the minimum version, by definition; don't check against
 * RPCAP_MIN_VERSION, as you may get compiler warnings that the
 * comparison will always succeed.
 */
#if RPCAP_MIN_VERSION == 0
#define RPCAP_VERSION_IS_SUPPORTED(v)	((v) <= RPCAP_MAX_VERSION)
#else
#define RPCAP_VERSION_IS_SUPPORTED(v)	\
	((v) >= RPCAP_MIN_VERSION && (v) <= RPCAP_MAX_VERSION)
#endif

/*
 * Separators used for the host list.
 *
 * It is used:
 * - by the rpcapd daemon, when you types a list of allowed connecting hosts
 * - by the rpcap client in active mode, when the client waits for incoming
 * connections from other hosts
 */
#define RPCAP_HOSTLIST_SEP " ,;\n\r"

/*********************************************************
 *                                                       *
 * Protocol messages formats                             *
 *                                                       *
 *********************************************************/
/*
 * WARNING: This file defines some structures that are used to transfer
 * data on the network.
 * Note that your compiler MUST not insert padding into these structures
 * for better alignment.
 * These structures have been created in order to be correctly aligned to
 * a 32-bit boundary, but be careful in any case.
 */

/*
 * WARNING: These typedefs MUST be of a specific size.
 * You might have to change them on your platform.
 *
 * XXX - use the C99 types?  Microsoft's newer versions of Visual Studio
 * support them.
 */
typedef unsigned char uint8;	/* 8-bit unsigned integer */
typedef unsigned short uint16;	/* 16-bit unsigned integer */
typedef unsigned int uint32;	/* 32-bit unsigned integer */
typedef int int32;		/* 32-bit signed integer */

/* Common header for all the RPCAP messages */
struct rpcap_header
{
	uint8 ver;	/* RPCAP version number */
	uint8 type;	/* RPCAP message type (error, findalldevs, ...) */
	uint16 value;	/* Message-dependent value (not always used) */
	uint32 plen;	/* Length of the payload of this RPCAP message */
};

/* Format of the message for the interface description (findalldevs command) */
struct rpcap_findalldevs_if
{
	uint16 namelen;	/* Length of the interface name */
	uint16 desclen;	/* Length of the interface description */
	uint32 flags;	/* Interface flags */
	uint16 naddr;	/* Number of addresses */
	uint16 dummy;	/* Must be zero */
};

/*
 * Format of an address as sent over the wire.
 *
 * Do *NOT* use struct sockaddr_storage, as the layout for that is
 * machine-dependent.
 *
 * RFC 2553 gives two sample layouts, both of which are 128 bytes long,
 * both of which are aligned on an 8-byte boundary, and both of which
 * have 2 bytes before the address data.
 *
 * However, one has a 2-byte address family value at the beginning
 * and the other has a 1-byte address length value and a 1-byte
 * address family value; this reflects the fact that the original
 * BSD sockaddr structure had a 2-byte address family value, which
 * was later changed to a 1-byte address length value and a 1-byte
 * address family value, when support for variable-length OSI
 * network-layer addresses was added.
 *
 * Furthermore, Solaris's struct sockaddr_storage is 256 bytes
 * long.
 *
 * This structure is supposed to be aligned on an 8-byte boundary;
 * the message header is 8 bytes long, so we don't have to do
 * anything to ensure it's aligned on that boundary within a packet,
 * so we just define it as 128 bytes long, with a 2-byte address
 * family.  (We only support IPv4 and IPv6 addresses, which are fixed-
 * length.)  That way, it's the same size as sockaddr_storage on
 * Windows, and it'll look like what an older Windows client will
 * expect.
 *
 * In addition, do *NOT* use the host's AF_ value for an address,
 * as the value for AF_INET6 is machine-dependent.  We use the
 * Windows value, so it'll look like what an older Windows client
 * will expect.
 *
 * (The Windows client is the only one that has been distributed
 * as a standard part of *pcap; UN*X clients are probably built
 * from source by the user or administrator, so they're in a
 * better position to upgrade an old client.  Therefore, we
 * try to make what goes over the wire look like what comes
 * from a Windows server.)
 */
struct rpcap_sockaddr
{
	uint16	family;			/* Address family */
	char	data[128-2];		/* Data */
};

/*
 * Format of an IPv4 address as sent over the wire.
 */
#define RPCAP_AF_INET	2		/* Value on all OSes */
struct rpcap_sockaddr_in
{
	uint16	family;			/* Address family */
	uint16	port;			/* Port number */
	uint32	addr;			/* IPv4 address */
	uint8	zero[8];		/* Padding */
};

/*
 * Format of an IPv6 address as sent over the wire.
 */
#define RPCAP_AF_INET6	23		/* Value on Windows */
struct rpcap_sockaddr_in6
{
	uint16	family;			/* Address family */
	uint16	port;			/* Port number */
	uint32	flowinfo;		/* IPv6 flow information */
	uint8	addr[16];		/* IPv6 address */
	uint32	scope_id;		/* Scope zone index */
};

/* Format of the message for the address listing (findalldevs command) */
struct rpcap_findalldevs_ifaddr
{
	struct rpcap_sockaddr addr;		/* Network address */
	struct rpcap_sockaddr netmask;		/* Netmask for that address */
	struct rpcap_sockaddr broadaddr;	/* Broadcast address for that address */
	struct rpcap_sockaddr dstaddr;		/* P2P destination address for that address */
};

/*
 * \brief Format of the message of the connection opening reply (open command).
 *
 * This structure transfers over the network some of the values useful on the client side.
 */
struct rpcap_openreply
{
	int32 linktype;	/* Link type */
	int32 tzoff;	/* Timezone offset */
};

/* Format of the message that starts a remote capture (startcap command) */
struct rpcap_startcapreq
{
	uint32 snaplen;		/* Length of the snapshot (number of bytes to capture for each packet) */
	uint32 read_timeout;	/* Read timeout in milliseconds */
	uint16 flags;		/* Flags (see RPCAP_STARTCAPREQ_FLAG_xxx) */
	uint16 portdata;	/* Network port on which the client is waiting at (if 'serveropen') */
};

/* Format of the reply message that devoted to start a remote capture (startcap reply command) */
struct rpcap_startcapreply
{
	int32 bufsize;		/* Size of the user buffer allocated by WinPcap; it can be different from the one we chose */
	uint16 portdata;	/* Network port on which the server is waiting at (passive mode only) */
	uint16 dummy;		/* Must be zero */
};

/*
 * \brief Format of the header which encapsulates captured packets when transmitted on the network.
 *
 * This message requires the general header as well, since we want to be able to exchange
 * more information across the network in the future (for example statistics, and kind like that).
 */
struct rpcap_pkthdr
{
	uint32 timestamp_sec;	/* 'struct timeval' compatible, it represents the 'tv_sec' field */
	uint32 timestamp_usec;	/* 'struct timeval' compatible, it represents the 'tv_usec' field */
	uint32 caplen;		/* Length of portion present in the capture */
	uint32 len;		/* Real length this packet (off wire) */
	uint32 npkt;		/* Ordinal number of the packet (i.e. the first one captured has '1', the second one '2', etc) */
};

/* General header used for the pcap_setfilter() command; keeps just the number of BPF instructions */
struct rpcap_filter
{
	uint16 filtertype;	/* type of the filter transferred (BPF instructions, ...) */
	uint16 dummy;		/* Must be zero */
	uint32 nitems;		/* Number of items contained into the filter (e.g. BPF instructions for BPF filters) */
};

/* Structure that keeps a single BPF instuction; it is repeated 'ninsn' times according to the 'rpcap_filterbpf' header */
struct rpcap_filterbpf_insn
{
	uint16 code;	/* opcode of the instruction */
	uint8 jt;	/* relative offset to jump to in case of 'true' */
	uint8 jf;	/* relative offset to jump to in case of 'false' */
	int32 k;	/* instruction-dependent value */
};

/* Structure that keeps the data required for the authentication on the remote host */
struct rpcap_auth
{
	uint16 type;	/* Authentication type */
	uint16 dummy;	/* Must be zero */
	uint16 slen1;	/* Length of the first authentication item (e.g. username) */
	uint16 slen2;	/* Length of the second authentication item (e.g. password) */
};

/* Structure that keeps the statistics about the number of packets captured, dropped, etc. */
struct rpcap_stats
{
	uint32 ifrecv;		/* Packets received by the kernel filter (i.e. pcap_stats.ps_recv) */
	uint32 ifdrop;		/* Packets dropped by the network interface (e.g. not enough buffers) (i.e. pcap_stats.ps_ifdrop) */
	uint32 krnldrop;	/* Packets dropped by the kernel filter (i.e. pcap_stats.ps_drop) */
	uint32 svrcapt;		/* Packets captured by the RPCAP daemon and sent on the network */
};

/* Structure that is needed to set sampling parameters */
struct rpcap_sampling
{
	uint8 method;	/* Sampling method */
	uint8 dummy1;	/* Must be zero */
	uint16 dummy2;	/* Must be zero */
	uint32 value;	/* Parameter related to the sampling method */
};

/* Messages field coding */
#define RPCAP_MSG_IS_REPLY		0x080	/* Flag indicating a reply */

#define RPCAP_MSG_ERROR			1	/* Message that keeps an error notification */
#define RPCAP_MSG_FINDALLIF_REQ		2	/* Request to list all the remote interfaces */
#define RPCAP_MSG_OPEN_REQ		3	/* Request to open a remote device */
#define RPCAP_MSG_STARTCAP_REQ		4	/* Request to start a capture on a remote device */
#define RPCAP_MSG_UPDATEFILTER_REQ	5	/* Send a compiled filter into the remote device */
#define RPCAP_MSG_CLOSE			6	/* Close the connection with the remote peer */
#define RPCAP_MSG_PACKET		7	/* This is a 'data' message, which carries a network packet */
#define RPCAP_MSG_AUTH_REQ		8	/* Message that keeps the authentication parameters */
#define RPCAP_MSG_STATS_REQ		9	/* It requires to have network statistics */
#define RPCAP_MSG_ENDCAP_REQ		10	/* Stops the current capture, keeping the device open */
#define RPCAP_MSG_SETSAMPLING_REQ	11	/* Set sampling parameters */

#define RPCAP_MSG_FINDALLIF_REPLY	(RPCAP_MSG_FINDALLIF_REQ | RPCAP_MSG_IS_REPLY)		/* Keeps the list of all the remote interfaces */
#define RPCAP_MSG_OPEN_REPLY		(RPCAP_MSG_OPEN_REQ | RPCAP_MSG_IS_REPLY)		/* The remote device has been opened correctly */
#define RPCAP_MSG_STARTCAP_REPLY	(RPCAP_MSG_STARTCAP_REQ | RPCAP_MSG_IS_REPLY)		/* The capture is starting correctly */
#define RPCAP_MSG_UPDATEFILTER_REPLY	(RPCAP_MSG_UPDATEFILTER_REQ | RPCAP_MSG_IS_REPLY)	/* The filter has been applied correctly on the remote device */
#define RPCAP_MSG_AUTH_REPLY		(RPCAP_MSG_AUTH_REQ | RPCAP_MSG_IS_REPLY)		/* Sends a message that says 'ok, authorization successful' */
#define RPCAP_MSG_STATS_REPLY		(RPCAP_MSG_STATS_REQ | RPCAP_MSG_IS_REPLY)		/* Message that keeps the network statistics */
#define RPCAP_MSG_ENDCAP_REPLY		(RPCAP_MSG_ENDCAP_REQ | RPCAP_MSG_IS_REPLY)		/* Confirms that the capture stopped successfully */
#define RPCAP_MSG_SETSAMPLING_REPLY	(RPCAP_MSG_SETSAMPLING_REQ | RPCAP_MSG_IS_REPLY)		/* Confirms that the capture stopped successfully */

#define RPCAP_STARTCAPREQ_FLAG_PROMISC		0x00000001	/* Enables promiscuous mode (default: disabled) */
#define RPCAP_STARTCAPREQ_FLAG_DGRAM		0x00000002	/* Use a datagram (i.e. UDP) connection for the data stream (default: use TCP)*/
#define RPCAP_STARTCAPREQ_FLAG_SERVEROPEN	0x00000004	/* The server has to open the data connection toward the client */
#define RPCAP_STARTCAPREQ_FLAG_INBOUND		0x00000008	/* Capture only inbound packets (take care: the flag has no effect with promiscuous enabled) */
#define RPCAP_STARTCAPREQ_FLAG_OUTBOUND		0x00000010	/* Capture only outbound packets (take care: the flag has no effect with promiscuous enabled) */

#define RPCAP_UPDATEFILTER_BPF 1			/* This code tells us that the filter is encoded with the BPF/NPF syntax */

/* Network error codes */
#define PCAP_ERR_NETW		1	/* Network error */
#define PCAP_ERR_INITTIMEOUT	2	/* The RPCAP initial timeout has expired */
#define PCAP_ERR_AUTH		3	/* Generic authentication error */
#define PCAP_ERR_FINDALLIF	4	/* Generic findalldevs error */
#define PCAP_ERR_NOREMOTEIF	5	/* The findalldevs was ok, but the remote end had no interfaces to list */
#define PCAP_ERR_OPEN		6	/* Generic pcap_open error */
#define PCAP_ERR_UPDATEFILTER	7	/* Generic updatefilter error */
#define PCAP_ERR_GETSTATS	8	/* Generic pcap_stats error */
#define PCAP_ERR_READEX		9	/* Generic pcap_next_ex error */
#define PCAP_ERR_HOSTNOAUTH	10	/* The host is not authorized to connect to this server */
#define PCAP_ERR_REMOTEACCEPT	11	/* Generic pcap_remoteaccept error */
#define PCAP_ERR_STARTCAPTURE	12	/* Generic pcap_startcapture error */
#define PCAP_ERR_ENDCAPTURE	13	/* Generic pcap_endcapture error */
#define PCAP_ERR_RUNTIMETIMEOUT	14	/* The RPCAP run-time timeout has expired */
#define PCAP_ERR_SETSAMPLING	15	/* Error during the settings of sampling parameters */
#define PCAP_ERR_WRONGMSG	16	/* The other end endpoint sent a message which has not been recognized */
#define PCAP_ERR_WRONGVER	17	/* The other end endpoint has a version number that is not compatible with our */

/*
 * \brief Buffer used by socket functions to send-receive packets.
 * In case you plan to have messages larger than this value, you have to increase it.
 */
#define RPCAP_NETBUF_SIZE 64000

/*********************************************************
 *                                                       *
 * Routines used by the rpcap client and rpcap daemon    *
 *                                                       *
 *********************************************************/

#include "sockutils.h"

extern void rpcap_createhdr(struct rpcap_header *header, uint8 ver, uint8 type, uint16 value, uint32 length);
extern const char *rpcap_msg_type_string(uint8 type);
extern int rpcap_senderror(SOCKET sock, uint8 ver, uint16 errcode, const char *error, char *errbuf);

#endif
