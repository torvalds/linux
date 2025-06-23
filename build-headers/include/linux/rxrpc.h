/* SPDX-License-Identifier: GPL-2.0-or-later WITH Linux-syscall-note */
/* Types and definitions for AF_RXRPC.
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _LINUX_RXRPC_H
#define _LINUX_RXRPC_H

#include <linux/types.h>
#include <linux/in.h>
#include <linux/in6.h>

/*
 * RxRPC socket address
 */
struct sockaddr_rxrpc {
	__kernel_sa_family_t	srx_family;	/* address family */
	__u16			srx_service;	/* service desired */
	__u16			transport_type;	/* type of transport socket (SOCK_DGRAM) */
	__u16			transport_len;	/* length of transport address */
	union {
		__kernel_sa_family_t family;	/* transport address family */
		struct sockaddr_in sin;		/* IPv4 transport address */
		struct sockaddr_in6 sin6;	/* IPv6 transport address */
	} transport;
};

/*
 * RxRPC socket options
 */
#define RXRPC_SECURITY_KEY		1	/* [clnt] set client security key */
#define RXRPC_SECURITY_KEYRING		2	/* [srvr] set ring of server security keys */
#define RXRPC_EXCLUSIVE_CONNECTION	3	/* Deprecated; use RXRPC_EXCLUSIVE_CALL instead */
#define RXRPC_MIN_SECURITY_LEVEL	4	/* minimum security level */
#define RXRPC_UPGRADEABLE_SERVICE	5	/* Upgrade service[0] -> service[1] */
#define RXRPC_SUPPORTED_CMSG		6	/* Get highest supported control message type */

/*
 * RxRPC control messages
 * - If neither abort or accept are specified, the message is a data message.
 * - terminal messages mean that a user call ID tag can be recycled
 * - s/r/- indicate whether these are applicable to sendmsg() and/or recvmsg()
 */
enum rxrpc_cmsg_type {
	RXRPC_USER_CALL_ID	= 1,	/* sr: user call ID specifier */
	RXRPC_ABORT		= 2,	/* sr: abort request / notification [terminal] */
	RXRPC_ACK		= 3,	/* -r: [Service] RPC op final ACK received [terminal] */
	RXRPC_NET_ERROR		= 5,	/* -r: network error received [terminal] */
	RXRPC_BUSY		= 6,	/* -r: server busy received [terminal] */
	RXRPC_LOCAL_ERROR	= 7,	/* -r: local error generated [terminal] */
	RXRPC_NEW_CALL		= 8,	/* -r: [Service] new incoming call notification */
	RXRPC_EXCLUSIVE_CALL	= 10,	/* s-: Call should be on exclusive connection */
	RXRPC_UPGRADE_SERVICE	= 11,	/* s-: Request service upgrade for client call */
	RXRPC_TX_LENGTH		= 12,	/* s-: Total length of Tx data */
	RXRPC_SET_CALL_TIMEOUT	= 13,	/* s-: Set one or more call timeouts */
	RXRPC_CHARGE_ACCEPT	= 14,	/* s-: Charge the accept pool with a user call ID */
	RXRPC__SUPPORTED
};

/*
 * RxRPC security levels
 */
#define RXRPC_SECURITY_PLAIN	0	/* plain secure-checksummed packets only */
#define RXRPC_SECURITY_AUTH	1	/* authenticated packets */
#define RXRPC_SECURITY_ENCRYPT	2	/* encrypted packets */

/*
 * RxRPC security indices
 */
#define RXRPC_SECURITY_NONE	0	/* no security protocol */
#define RXRPC_SECURITY_RXKAD	2	/* kaserver or kerberos 4 */
#define RXRPC_SECURITY_RXGK	4	/* gssapi-based */
#define RXRPC_SECURITY_RXK5	5	/* kerberos 5 */

/*
 * RxRPC-level abort codes
 */
#define RX_CALL_DEAD		-1	/* call/conn has been inactive and is shut down */
#define RX_INVALID_OPERATION	-2	/* invalid operation requested / attempted */
#define RX_CALL_TIMEOUT		-3	/* call timeout exceeded */
#define RX_EOF			-4	/* unexpected end of data on read op */
#define RX_PROTOCOL_ERROR	-5	/* low-level protocol error */
#define RX_USER_ABORT		-6	/* generic user abort */
#define RX_ADDRINUSE		-7	/* UDP port in use */
#define RX_DEBUGI_BADTYPE	-8	/* bad debugging packet type */

/*
 * (un)marshalling abort codes (rxgen)
 */
#define RXGEN_CC_MARSHAL	-450
#define RXGEN_CC_UNMARSHAL	-451
#define RXGEN_SS_MARSHAL	-452
#define RXGEN_SS_UNMARSHAL	-453
#define RXGEN_DECODE		-454
#define RXGEN_OPCODE		-455
#define RXGEN_SS_XDRFREE	-456
#define RXGEN_CC_XDRFREE	-457

/*
 * Rx kerberos security abort codes
 * - unfortunately we have no generalised security abort codes to say things
 *   like "unsupported security", so we have to use these instead and hope the
 *   other side understands
 */
#define RXKADINCONSISTENCY	19270400	/* security module structure inconsistent */
#define RXKADPACKETSHORT	19270401	/* packet too short for security challenge */
#define RXKADLEVELFAIL		19270402	/* security level negotiation failed */
#define RXKADTICKETLEN		19270403	/* ticket length too short or too long */
#define RXKADOUTOFSEQUENCE	19270404	/* packet had bad sequence number */
#define RXKADNOAUTH		19270405	/* caller not authorised */
#define RXKADBADKEY		19270406	/* illegal key: bad parity or weak */
#define RXKADBADTICKET		19270407	/* security object was passed a bad ticket */
#define RXKADUNKNOWNKEY		19270408	/* ticket contained unknown key version number */
#define RXKADEXPIRED		19270409	/* authentication expired */
#define RXKADSEALEDINCON	19270410	/* sealed data inconsistent */
#define RXKADDATALEN		19270411	/* user data too long */
#define RXKADILLEGALLEVEL	19270412	/* caller not authorised to use encrypted conns */

#endif /* _LINUX_RXRPC_H */
