/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_SOCKET_H
#define _UAPI_ASM_SOCKET_H

#include <linux/posix_types.h>
#include <asm/sockios.h>

/* For setsockopt(2) */
#define SOL_SOCKET	0xffff

#define SO_DEBUG	0x0001
#define SO_REUSEADDR	0x0004
#define SO_KEEPALIVE	0x0008
#define SO_DONTROUTE	0x0010
#define SO_BROADCAST	0x0020
#define SO_LINGER	0x0080
#define SO_OOBINLINE	0x0100
#define SO_REUSEPORT	0x0200
#define SO_SNDBUF	0x1001
#define SO_RCVBUF	0x1002
#define SO_SNDBUFFORCE	0x100a
#define SO_RCVBUFFORCE	0x100b
#define SO_SNDLOWAT	0x1003
#define SO_RCVLOWAT	0x1004
#define SO_SNDTIMEO_OLD	0x1005
#define SO_RCVTIMEO_OLD	0x1006
#define SO_ERROR	0x1007
#define SO_TYPE		0x1008
#define SO_PROTOCOL	0x1028
#define SO_DOMAIN	0x1029
#define SO_PEERNAME	0x2000

#define SO_NO_CHECK	0x400b
#define SO_PRIORITY	0x400c
#define SO_BSDCOMPAT	0x400e
#define SO_PASSCRED	0x4010
#define SO_PEERCRED	0x4011

/* Security levels - as per NRL IPv6 - don't actually do anything */
#define SO_SECURITY_AUTHENTICATION		0x4016
#define SO_SECURITY_ENCRYPTION_TRANSPORT	0x4017
#define SO_SECURITY_ENCRYPTION_NETWORK		0x4018

#define SO_BINDTODEVICE	0x4019

/* Socket filtering */
#define SO_ATTACH_FILTER        0x401a
#define SO_DETACH_FILTER        0x401b
#define SO_GET_FILTER		SO_ATTACH_FILTER

#define SO_ACCEPTCONN		0x401c

#define SO_PEERSEC		0x401d
#define SO_PASSSEC		0x401e

#define SO_MARK			0x401f

#define SO_RXQ_OVFL             0x4021

#define SO_WIFI_STATUS		0x4022
#define SCM_WIFI_STATUS		SO_WIFI_STATUS
#define SO_PEEK_OFF		0x4023

/* Instruct lower device to use last 4-bytes of skb data as FCS */
#define SO_NOFCS		0x4024

#define SO_LOCK_FILTER		0x4025

#define SO_SELECT_ERR_QUEUE	0x4026

#define SO_BUSY_POLL		0x4027

#define SO_MAX_PACING_RATE	0x4028

#define SO_BPF_EXTENSIONS	0x4029

#define SO_INCOMING_CPU		0x402A

#define SO_ATTACH_BPF		0x402B
#define SO_DETACH_BPF		SO_DETACH_FILTER

#define SO_ATTACH_REUSEPORT_CBPF	0x402C
#define SO_ATTACH_REUSEPORT_EBPF	0x402D

#define SO_CNX_ADVICE		0x402E

#define SCM_TIMESTAMPING_OPT_STATS	0x402F

#define SO_MEMINFO		0x4030

#define SO_INCOMING_NAPI_ID	0x4031

#define SO_COOKIE		0x4032

#define SCM_TIMESTAMPING_PKTINFO	0x4033

#define SO_PEERGROUPS		0x4034

#define SO_ZEROCOPY		0x4035

#define SO_TXTIME		0x4036
#define SCM_TXTIME		SO_TXTIME

#define SO_BINDTOIFINDEX	0x4037

#define SO_TIMESTAMP_OLD        0x4012
#define SO_TIMESTAMPNS_OLD      0x4013
#define SO_TIMESTAMPING_OLD     0x4020

#define SO_TIMESTAMP_NEW        0x4038
#define SO_TIMESTAMPNS_NEW      0x4039
#define SO_TIMESTAMPING_NEW     0x403A

#define SO_RCVTIMEO_NEW         0x4040
#define SO_SNDTIMEO_NEW         0x4041

#define SO_DETACH_REUSEPORT_BPF 0x4042

#define SO_PREFER_BUSY_POLL	0x4043
#define SO_BUSY_POLL_BUDGET	0x4044

#define SO_NETNS_COOKIE		0x4045

#define SO_BUF_LOCK		0x4046

#define SO_RESERVE_MEM		0x4047

#define SO_TXREHASH		0x4048

#define SO_RCVMARK		0x4049

#define SO_PASSPIDFD		0x404A
#define SO_PEERPIDFD		0x404B

#define SCM_TS_OPT_ID		0x404C

#define SO_RCVPRIORITY		0x404D

#define SO_DEVMEM_LINEAR	0x404E
#define SCM_DEVMEM_LINEAR	SO_DEVMEM_LINEAR
#define SO_DEVMEM_DMABUF	0x404F
#define SCM_DEVMEM_DMABUF	SO_DEVMEM_DMABUF
#define SO_DEVMEM_DONTNEED	0x4050

#if !defined(__KERNEL__)

#if __BITS_PER_LONG == 64
#define SO_TIMESTAMP		SO_TIMESTAMP_OLD
#define SO_TIMESTAMPNS		SO_TIMESTAMPNS_OLD
#define SO_TIMESTAMPING         SO_TIMESTAMPING_OLD
#define SO_RCVTIMEO		SO_RCVTIMEO_OLD
#define SO_SNDTIMEO		SO_SNDTIMEO_OLD
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
