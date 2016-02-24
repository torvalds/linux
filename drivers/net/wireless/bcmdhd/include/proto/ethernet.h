/*
 * From FreeBSD 2.2.7: Fundamental constants relating to ethernet.
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: ethernet.h 403353 2013-05-20 14:05:33Z $
 */

#ifndef _NET_ETHERNET_H_	/* use native BSD ethernet.h when available */
#define _NET_ETHERNET_H_

#ifndef _TYPEDEFS_H_
#include "typedefs.h"
#endif

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>


/*
 * The number of bytes in an ethernet (MAC) address.
 */
#define	ETHER_ADDR_LEN		6

/*
 * The number of bytes in the type field.
 */
#define	ETHER_TYPE_LEN		2

/*
 * The number of bytes in the trailing CRC field.
 */
#define	ETHER_CRC_LEN		4

/*
 * The length of the combined header.
 */
#define	ETHER_HDR_LEN		(ETHER_ADDR_LEN * 2 + ETHER_TYPE_LEN)

/*
 * The minimum packet length.
 */
#define	ETHER_MIN_LEN		64

/*
 * The minimum packet user data length.
 */
#define	ETHER_MIN_DATA		46

/*
 * The maximum packet length.
 */
#define	ETHER_MAX_LEN		1518

/*
 * The maximum packet user data length.
 */
#define	ETHER_MAX_DATA		1500

/* ether types */
#define ETHER_TYPE_MIN		0x0600		/* Anything less than MIN is a length */
#define	ETHER_TYPE_IP		0x0800		/* IP */
#define ETHER_TYPE_ARP		0x0806		/* ARP */
#define ETHER_TYPE_8021Q	0x8100		/* 802.1Q */
#define	ETHER_TYPE_IPV6		0x86dd		/* IPv6 */
#define	ETHER_TYPE_BRCM		0x886c		/* Broadcom Corp. */
#define	ETHER_TYPE_802_1X	0x888e		/* 802.1x */
#ifdef PLC
#define	ETHER_TYPE_88E1		0x88e1		/* GIGLE */
#define	ETHER_TYPE_8912		0x8912		/* GIGLE */
#define ETHER_TYPE_GIGLED	0xffff		/* GIGLE */
#endif /* PLC */
#define	ETHER_TYPE_802_1X_PREAUTH 0x88c7	/* 802.1x preauthentication */
#define ETHER_TYPE_WAI		0x88b4		/* WAI */
#define ETHER_TYPE_89_0D	0x890d		/* 89-0d frame for TDLS */

#define ETHER_TYPE_PPP_SES	0x8864		/* PPPoE Session */

#define ETHER_TYPE_IAPP_L2_UPDATE	0x6	/* IAPP L2 update frame */

/* Broadcom subtype follows ethertype;  First 2 bytes are reserved; Next 2 are subtype; */
#define	ETHER_BRCM_SUBTYPE_LEN	4	/* Broadcom 4 byte subtype */

/* ether header */
#define ETHER_DEST_OFFSET	(0 * ETHER_ADDR_LEN)	/* dest address offset */
#define ETHER_SRC_OFFSET	(1 * ETHER_ADDR_LEN)	/* src address offset */
#define ETHER_TYPE_OFFSET	(2 * ETHER_ADDR_LEN)	/* ether type offset */

/*
 * A macro to validate a length with
 */
#define	ETHER_IS_VALID_LEN(foo)	\
	((foo) >= ETHER_MIN_LEN && (foo) <= ETHER_MAX_LEN)

#define ETHER_FILL_MCAST_ADDR_FROM_IP(ea, mgrp_ip) {		\
		((uint8 *)ea)[0] = 0x01;			\
		((uint8 *)ea)[1] = 0x00;			\
		((uint8 *)ea)[2] = 0x5e;			\
		((uint8 *)ea)[3] = ((mgrp_ip) >> 16) & 0x7f;	\
		((uint8 *)ea)[4] = ((mgrp_ip) >>  8) & 0xff;	\
		((uint8 *)ea)[5] = ((mgrp_ip) >>  0) & 0xff;	\
}

#ifndef __INCif_etherh /* Quick and ugly hack for VxWorks */
/*
 * Structure of a 10Mb/s Ethernet header.
 */
BWL_PRE_PACKED_STRUCT struct ether_header {
	uint8	ether_dhost[ETHER_ADDR_LEN];
	uint8	ether_shost[ETHER_ADDR_LEN];
	uint16	ether_type;
} BWL_POST_PACKED_STRUCT;

/*
 * Structure of a 48-bit Ethernet address.
 */
BWL_PRE_PACKED_STRUCT struct	ether_addr {
	uint8 octet[ETHER_ADDR_LEN];
} BWL_POST_PACKED_STRUCT;
#endif	/* !__INCif_etherh Quick and ugly hack for VxWorks */

/*
 * Takes a pointer, set, test, clear, toggle locally admininistered
 * address bit in the 48-bit Ethernet address.
 */
#define ETHER_SET_LOCALADDR(ea)	(((uint8 *)(ea))[0] = (((uint8 *)(ea))[0] | 2))
#define ETHER_IS_LOCALADDR(ea) 	(((uint8 *)(ea))[0] & 2)
#define ETHER_CLR_LOCALADDR(ea)	(((uint8 *)(ea))[0] = (((uint8 *)(ea))[0] & 0xfd))
#define ETHER_TOGGLE_LOCALADDR(ea)	(((uint8 *)(ea))[0] = (((uint8 *)(ea))[0] ^ 2))

/* Takes a pointer, marks unicast address bit in the MAC address */
#define ETHER_SET_UNICAST(ea)	(((uint8 *)(ea))[0] = (((uint8 *)(ea))[0] & ~1))

/*
 * Takes a pointer, returns true if a 48-bit multicast address
 * (including broadcast, since it is all ones)
 */
#define ETHER_ISMULTI(ea) (((const uint8 *)(ea))[0] & 1)


/* compare two ethernet addresses - assumes the pointers can be referenced as shorts */
#define eacmp(a, b)	((((const uint16 *)(a))[0] ^ ((const uint16 *)(b))[0]) | \
	                 (((const uint16 *)(a))[1] ^ ((const uint16 *)(b))[1]) | \
	                 (((const uint16 *)(a))[2] ^ ((const uint16 *)(b))[2]))

#define	ether_cmp(a, b)	eacmp(a, b)

/* copy an ethernet address - assumes the pointers can be referenced as shorts */
#define eacopy(s, d) \
do { \
	((uint16 *)(d))[0] = ((const uint16 *)(s))[0]; \
	((uint16 *)(d))[1] = ((const uint16 *)(s))[1]; \
	((uint16 *)(d))[2] = ((const uint16 *)(s))[2]; \
} while (0)

#define	ether_copy(s, d) eacopy(s, d)

/* Copy an ethernet address in reverse order */
#define	ether_rcopy(s, d) \
do { \
	((uint16 *)(d))[2] = ((uint16 *)(s))[2]; \
	((uint16 *)(d))[1] = ((uint16 *)(s))[1]; \
	((uint16 *)(d))[0] = ((uint16 *)(s))[0]; \
} while (0)



static const struct ether_addr ether_bcast = {{255, 255, 255, 255, 255, 255}};
static const struct ether_addr ether_null = {{0, 0, 0, 0, 0, 0}};
static const struct ether_addr ether_ipv6_mcast = {{0x33, 0x33, 0x00, 0x00, 0x00, 0x01}};

#define ETHER_ISBCAST(ea)	((((const uint8 *)(ea))[0] &		\
	                          ((const uint8 *)(ea))[1] &		\
				  ((const uint8 *)(ea))[2] &		\
				  ((const uint8 *)(ea))[3] &		\
				  ((const uint8 *)(ea))[4] &		\
				  ((const uint8 *)(ea))[5]) == 0xff)
#define ETHER_ISNULLADDR(ea)	((((const uint8 *)(ea))[0] |		\
				  ((const uint8 *)(ea))[1] |		\
				  ((const uint8 *)(ea))[2] |		\
				  ((const uint8 *)(ea))[3] |		\
				  ((const uint8 *)(ea))[4] |		\
				  ((const uint8 *)(ea))[5]) == 0)

#define ETHER_ISNULLDEST(da)	((((const uint16 *)(da))[0] |           \
				  ((const uint16 *)(da))[1] |           \
				  ((const uint16 *)(da))[2]) == 0)
#define ETHER_ISNULLSRC(sa)	ETHER_ISNULLDEST(sa)

#define ETHER_MOVE_HDR(d, s) \
do { \
	struct ether_header t; \
	t = *(struct ether_header *)(s); \
	*(struct ether_header *)(d) = t; \
} while (0)

#define  ETHER_ISUCAST(ea) ((((uint8 *)(ea))[0] & 0x01) == 0)

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif /* _NET_ETHERNET_H_ */
