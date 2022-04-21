/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1999, 2000, 2001 Ralf Baechle
 * Copyright (C) 2000, 2001 Silicon Graphics, Inc.
 */
#ifndef _UAPI_ASM_SOCKET_H
#define _UAPI_ASM_SOCKET_H

#include <linux/posix_types.h>
#include <asm/sockios.h>

/*
 * For setsockopt(2)
 *
 * This defines are ABI conformant as far as Linux supports these ...
 */
#define SOL_SOCKET	0xffff

#define SO_DEBUG	0x0001	/* Record debugging information.  */
#define SO_REUSEADDR	0x0004	/* Allow reuse of local addresses.  */
#define SO_KEEPALIVE	0x0008	/* Keep connections alive and send
				   SIGPIPE when they die.  */
#define SO_DONTROUTE	0x0010	/* Don't do local routing.  */
#define SO_BROADCAST	0x0020	/* Allow transmission of
				   broadcast messages.	*/
#define SO_LINGER	0x0080	/* Block on close of a reliable
				   socket to transmit pending data.  */
#define SO_OOBINLINE 0x0100	/* Receive out-of-band data in-band.  */
#define SO_REUSEPORT 0x0200	/* Allow local address and port reuse.  */

#define SO_TYPE		0x1008	/* Compatible name for SO_STYLE.  */
#define SO_STYLE	SO_TYPE /* Synonym */
#define SO_ERROR	0x1007	/* get error status and clear */
#define SO_SNDBUF	0x1001	/* Send buffer size. */
#define SO_RCVBUF	0x1002	/* Receive buffer. */
#define SO_SNDLOWAT	0x1003	/* send low-water mark */
#define SO_RCVLOWAT	0x1004	/* receive low-water mark */
#define SO_SNDTIMEO_OLD	0x1005	/* send timeout */
#define SO_RCVTIMEO_OLD	0x1006	/* receive timeout */
#define SO_ACCEPTCONN	0x1009
#define SO_PROTOCOL	0x1028	/* protocol type */
#define SO_DOMAIN	0x1029	/* domain/socket family */

/* linux-specific, might as well be the same as on i386 */
#define SO_NO_CHECK	11
#define SO_PRIORITY	12
#define SO_BSDCOMPAT	14

#define SO_PASSCRED	17
#define SO_PEERCRED	18

/* Security levels - as per NRL IPv6 - don't actually do anything */
#define SO_SECURITY_AUTHENTICATION		22
#define SO_SECURITY_ENCRYPTION_TRANSPORT	23
#define SO_SECURITY_ENCRYPTION_NETWORK		24

#define SO_BINDTODEVICE		25

/* Socket filtering */
#define SO_ATTACH_FILTER	26
#define SO_DETACH_FILTER	27
#define SO_GET_FILTER		SO_ATTACH_FILTER

#define SO_PEERNAME		28

#define SO_PEERSEC		30
#define SO_SNDBUFFORCE		31
#define SO_RCVBUFFORCE		33
#define SO_PASSSEC		34

#define SO_MARK			36

#define SO_RXQ_OVFL		40

#define SO_WIFI_STATUS		41
#define SCM_WIFI_STATUS		SO_WIFI_STATUS
#define SO_PEEK_OFF		42

/* Instruct lower device to use last 4-bytes of skb data as FCS */
#define SO_NOFCS		43

#define SO_LOCK_FILTER		44

#define SO_SELECT_ERR_QUEUE	45

#define SO_BUSY_POLL		46

#define SO_MAX_PACING_RATE	47

#define SO_BPF_EXTENSIONS	48

#define SO_INCOMING_CPU		49

#define SO_ATTACH_BPF		50
#define SO_DETACH_BPF		SO_DETACH_FILTER

#define SO_ATTACH_REUSEPORT_CBPF	51
#define SO_ATTACH_REUSEPORT_EBPF	52

#define SO_CNX_ADVICE		53

#define SCM_TIMESTAMPING_OPT_STATS	54

#define SO_MEMINFO		55

#define SO_INCOMING_NAPI_ID	56

#define SO_COOKIE		57

#define SCM_TIMESTAMPING_PKTINFO	58

#define SO_PEERGROUPS		59

#define SO_ZEROCOPY		60

#define SO_TXTIME		61
#define SCM_TXTIME		SO_TXTIME

#define SO_BINDTOIFINDEX	62

#define SO_TIMESTAMP_OLD        29
#define SO_TIMESTAMPNS_OLD      35
#define SO_TIMESTAMPING_OLD     37

#define SO_TIMESTAMP_NEW        63
#define SO_TIMESTAMPNS_NEW      64
#define SO_TIMESTAMPING_NEW     65

#define SO_RCVTIMEO_NEW         66
#define SO_SNDTIMEO_NEW         67

#define SO_DETACH_REUSEPORT_BPF 68

#define SO_PREFER_BUSY_POLL	69
#define SO_BUSY_POLL_BUDGET	70

#define SO_NETNS_COOKIE		71

#define SO_BUF_LOCK		72

#define SO_RESERVE_MEM		73

#define SO_TXREHASH		74

#if !defined(__KERNEL__)

#if __BITS_PER_LONG == 64
#define SO_TIMESTAMP		SO_TIMESTAMP_OLD
#define SO_TIMESTAMPNS		SO_TIMESTAMPNS_OLD
#define SO_TIMESTAMPING		SO_TIMESTAMPING_OLD

#define SO_RCVTIMEO             SO_RCVTIMEO_OLD
#define SO_SNDTIMEO             SO_SNDTIMEO_OLD
#else
#define SO_TIMESTAMP (sizeof(time_t) == sizeof(__kernel_long_t) ? SO_TIMESTAMP_OLD : SO_TIMESTAMP_NEW)
#define SO_TIMESTAMPNS (sizeof(time_t) == sizeof(__kernel_long_t) ? SO_TIMESTAMPNS_OLD : SO_TIMESTAMPNS_NEW)
#define SO_TIMESTAMPING (sizeof(time_t) == sizeof(__kernel_long_t) ? SO_TIMESTAMPING_OLD : SO_TIMESTAMPING_NEW)

#define SO_RCVTIMEO (sizeof(time_t) == sizeof(__kernel_long_t) ? SO_RCVTIMEO_OLD : SO_RCVTIMEO_NEW)
#define SO_SNDTIMEO (sizeof(time_t) == sizeof(__kernel_long_t) ? SO_SNDTIMEO_OLD : SO_SNDTIMEO_NEW)
#endif

#define SCM_TIMESTAMP           SO_TIMESTAMP
#define SCM_TIMESTAMPNS         SO_TIMESTAMPNS
#define SCM_TIMESTAMPING        SO_TIMESTAMPING

#endif

#endif /* _UAPI_ASM_SOCKET_H */
