#ifndef _ASM_SOCKET_H
#define _ASM_SOCKET_H

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
#define SO_RCVTIMEO     0x2000
#define SO_SNDTIMEO     0x4000
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
#define SO_TIMESTAMP		0x001d
#define SCM_TIMESTAMP		SO_TIMESTAMP

#define SO_PEERSEC		0x001e
#define SO_PASSSEC		0x001f
#define SO_TIMESTAMPNS		0x0021
#define SCM_TIMESTAMPNS		SO_TIMESTAMPNS

#define SO_MARK			0x0022

#define SO_TIMESTAMPING		0x0023
#define SCM_TIMESTAMPING	SO_TIMESTAMPING

#define SO_RXQ_OVFL             0x0024

#define SO_WIFI_STATUS		0x0025
#define SCM_WIFI_STATUS		SO_WIFI_STATUS
#define SO_PEEK_OFF		0x0026

/* Instruct lower device to use last 4-bytes of skb data as FCS */
#define SO_NOFCS		0x0027

#define SO_LOCK_FILTER		0x0028

#define SO_SELECT_ERR_QUEUE	0x0029

#define SO_LL			0x0030

/* Security levels - as per NRL IPv6 - don't actually do anything */
#define SO_SECURITY_AUTHENTICATION		0x5001
#define SO_SECURITY_ENCRYPTION_TRANSPORT	0x5002
#define SO_SECURITY_ENCRYPTION_NETWORK		0x5004

#endif /* _ASM_SOCKET_H */
