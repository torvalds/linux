/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_SOCKET_H
#define _ASM_SOCKET_H

#include <linux/posix_types.h>
#include <asm/sockios.h>

/* For setsockopt(2) */
#define SOL_SOCKET	0xffff

#define SO_DEBUG	0x0001
#define SO_PASSCRED	0x0002
#define SO_REUSEADDR	0x0004
#define SO_KEEPALIVE	0x0008
#define SO_DONTROUTE	0x0010
#define SO_BROADCAST	0x0020
#define SO_PEERCRED	0x0040
#define SO_LINGER	0x0080
#define SO_OOBINLINE	0x0100
#define SO_REUSEPORT	0x0200
#define SO_BSDCOMPAT    0x0400
#define SO_RCVLOWAT     0x0800
#define SO_SNDLOWAT     0x1000
#define SO_RCVTIMEO_OLD     0x2000
#define SO_SNDTIMEO_OLD     0x4000
#define SO_ACCEPTCONN	0x8000

#define SO_SNDBUF	0x1001
#define SO_RCVBUF	0x1002
#define SO_SNDBUFFORCE	0x100a
#define SO_RCVBUFFORCE	0x100b
#define SO_ERROR	0x1007
#define SO_TYPE		0x1008
#define SO_PROTOCOL	0x1028
#define SO_DOMAIN	0x1029

/* Linux specific, keep the same. */
#define SO_NO_CHECK	0x000b
#define SO_PRIORITY	0x000c

#define SO_BINDTODEVICE 0x000d

#define SO_ATTACH_FILTER	0x001a
#define SO_DETACH_FILTER        0x001b
#define SO_GET_FILTER		SO_ATTACH_FILTER

#define SO_PEERNAME		0x001c

#define SO_PEERSEC		0x001e
#define SO_PASSSEC		0x001f

#define SO_MARK			0x0022

#define SO_RXQ_OVFL             0x0024

#define SO_WIFI_STATUS		0x0025
#define SCM_WIFI_STATUS		SO_WIFI_STATUS
#define SO_PEEK_OFF		0x0026

/* Instruct lower device to use last 4-bytes of skb data as FCS */
#define SO_NOFCS		0x0027

#define SO_LOCK_FILTER		0x0028

#define SO_SELECT_ERR_QUEUE	0x0029

#define SO_BUSY_POLL		0x0030

#define SO_MAX_PACING_RATE	0x0031

#define SO_BPF_EXTENSIONS	0x0032

#define SO_INCOMING_CPU		0x0033

#define SO_ATTACH_BPF		0x0034
#define SO_DETACH_BPF		SO_DETACH_FILTER

#define SO_ATTACH_REUSEPORT_CBPF	0x0035
#define SO_ATTACH_REUSEPORT_EBPF	0x0036

#define SO_CNX_ADVICE		0x0037

#define SCM_TIMESTAMPING_OPT_STATS	0x0038

#define SO_MEMINFO		0x0039

#define SO_INCOMING_NAPI_ID	0x003a

#define SO_COOKIE		0x003b

#define SCM_TIMESTAMPING_PKTINFO	0x003c

#define SO_PEERGROUPS		0x003d

#define SO_ZEROCOPY		0x003e

#define SO_TXTIME		0x003f
#define SCM_TXTIME		SO_TXTIME

#define SO_BINDTOIFINDEX	0x0041

/* Security levels - as per NRL IPv6 - don't actually do anything */
#define SO_SECURITY_AUTHENTICATION		0x5001
#define SO_SECURITY_ENCRYPTION_TRANSPORT	0x5002
#define SO_SECURITY_ENCRYPTION_NETWORK		0x5004

#define SO_TIMESTAMP_OLD         0x001d
#define SO_TIMESTAMPNS_OLD       0x0021
#define SO_TIMESTAMPING_OLD      0x0023

#define SO_TIMESTAMP_NEW         0x0046
#define SO_TIMESTAMPNS_NEW       0x0042
#define SO_TIMESTAMPING_NEW      0x0043

#define SO_RCVTIMEO_NEW          0x0044
#define SO_SNDTIMEO_NEW          0x0045

#define SO_DETACH_REUSEPORT_BPF  0x0047

#define SO_PREFER_BUSY_POLL	 0x0048
#define SO_BUSY_POLL_BUDGET	 0x0049

#define SO_NETNS_COOKIE          0x0050

#define SO_BUF_LOCK              0x0051

#define SO_RESEVE_MEM            0x0052


#if !defined(__KERNEL__)


#if __BITS_PER_LONG == 64
#define SO_TIMESTAMP		SO_TIMESTAMP_OLD
#define SO_TIMESTAMPNS		SO_TIMESTAMPNS_OLD
#define SO_TIMESTAMPING		SO_TIMESTAMPING_OLD

#define SO_RCVTIMEO		SO_RCVTIMEO_OLD
#define SO_SNDTIMEO		SO_SNDTIMEO_OLD
#else
#define SO_TIMESTAMP (sizeof(time_t) == sizeof(__kernel_long_t) ? SO_TIMESTAMP_OLD : SO_TIMESTAMP_NEW)
#define SO_TIMESTAMPNS (sizeof(time_t) == sizeof(__kernel_long_t) ? SO_TIMESTAMPNS_OLD : SO_TIMESTAMPNS_NEW)
#define SO_TIMESTAMPING (sizeof(time_t) == sizeof(__kernel_long_t) ? SO_TIMESTAMPING_OLD : SO_TIMESTAMPING_NEW)

#define SO_RCVTIMEO (sizeof(time_t) == sizeof(__kernel_long_t) ? SO_RCVTIMEO_OLD : SO_RCVTIMEO_NEW)
#define SO_SNDTIMEO (sizeof(time_t) == sizeof(__kernel_long_t) ? SO_SNDTIMEO_OLD : SO_SNDTIMEO_NEW)
#endif

#define SCM_TIMESTAMP          SO_TIMESTAMP
#define SCM_TIMESTAMPNS        SO_TIMESTAMPNS
#define SCM_TIMESTAMPING       SO_TIMESTAMPING

#endif

#endif /* _ASM_SOCKET_H */
