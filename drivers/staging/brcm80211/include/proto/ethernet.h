/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _NET_ETHERNET_H_
#define _NET_ETHERNET_H_

#include <packed_section_start.h>

#define	ETHER_ADDR_LEN		6
#define	ETHER_TYPE_LEN		2
#define	ETHER_CRC_LEN		4
#define	ETHER_HDR_LEN		(ETHER_ADDR_LEN * 2 + ETHER_TYPE_LEN)
#define	ETHER_MIN_LEN		64
#define	ETHER_MIN_DATA		46
#define	ETHER_MAX_LEN		1518
#define	ETHER_MAX_DATA		1500

#define ETHER_TYPE_MIN		0x0600
#define	ETHER_TYPE_IP		0x0800
#define ETHER_TYPE_ARP		0x0806
#define ETHER_TYPE_8021Q	0x8100
#define	ETHER_TYPE_BRCM		0x886c
#define	ETHER_TYPE_802_1X	0x888e
#define	ETHER_TYPE_802_1X_PREAUTH 0x88c7

#define ETHER_DEST_OFFSET	(0 * ETHER_ADDR_LEN)
#define ETHER_SRC_OFFSET	(1 * ETHER_ADDR_LEN)
#define ETHER_TYPE_OFFSET	(2 * ETHER_ADDR_LEN)

#define	ETHER_IS_VALID_LEN(foo)	\
	((foo) >= ETHER_MIN_LEN && (foo) <= ETHER_MAX_LEN)

#define ETHER_FILL_MCAST_ADDR_FROM_IP(ea, mgrp_ip) {		\
		((u8 *)ea)[0] = 0x01;			\
		((u8 *)ea)[1] = 0x00;			\
		((u8 *)ea)[2] = 0x5e;			\
		((u8 *)ea)[3] = ((mgrp_ip) >> 16) & 0x7f;	\
		((u8 *)ea)[4] = ((mgrp_ip) >>  8) & 0xff;	\
		((u8 *)ea)[5] = ((mgrp_ip) >>  0) & 0xff;	\
}

BWL_PRE_PACKED_STRUCT struct ether_header {
	u8 ether_dhost[ETHER_ADDR_LEN];
	u8 ether_shost[ETHER_ADDR_LEN];
	u16 ether_type;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct ether_addr {
	u8 octet[ETHER_ADDR_LEN];
} BWL_POST_PACKED_STRUCT;

#define ETHER_SET_LOCALADDR(ea)	(((u8 *)(ea))[0] = (((u8 *)(ea))[0] | 2))
#define ETHER_IS_LOCALADDR(ea) 	(((u8 *)(ea))[0] & 2)
#define ETHER_CLR_LOCALADDR(ea)	(((u8 *)(ea))[0] = \
	(((u8 *)(ea))[0] & 0xd))
#define ETHER_TOGGLE_LOCALADDR(ea)	(((u8 *)(ea))[0] = \
	(((u8 *)(ea))[0] ^ 2))

#define ETHER_SET_UNICAST(ea)	(((u8 *)(ea))[0] = (((u8 *)(ea))[0] & ~1))

#define ETHER_ISMULTI(ea) (((const u8 *)(ea))[0] & 1)

#define	ether_cmp(a, b)	(!(((short *)a)[0] == ((short *)b)[0]) | \
			 !(((short *)a)[1] == ((short *)b)[1]) | \
			 !(((short *)a)[2] == ((short *)b)[2]))

#define	ether_copy(s, d) { \
		((short *)d)[0] = ((short *)s)[0]; \
		((short *)d)[1] = ((short *)s)[1]; \
		((short *)d)[2] = ((short *)s)[2]; }

static const struct ether_addr ether_bcast = { {255, 255, 255, 255, 255, 255} };
static const struct ether_addr ether_null = { {0, 0, 0, 0, 0, 0} };

#define ETHER_ISBCAST(ea)	((((u8 *)(ea))[0] &		\
	((u8 *)(ea))[1] &		\
	((u8 *)(ea))[2] &		\
	((u8 *)(ea))[3] &		\
	((u8 *)(ea))[4] &		\
	((u8 *)(ea))[5]) == 0xff)
#define ETHER_ISNULLADDR(ea)	((((u8 *)(ea))[0] |		\
	((u8 *)(ea))[1] |		\
	((u8 *)(ea))[2] |		\
	((u8 *)(ea))[3] |		\
	((u8 *)(ea))[4] |		\
	((u8 *)(ea))[5]) == 0)

#define ETHER_MOVE_HDR(d, s) \
do { \
	struct ether_header t; \
	t = *(struct ether_header *)(s); \
	*(struct ether_header *)(d) = t; \
} while (0)

#include <packed_section_end.h>

#endif				/* _NET_ETHERNET_H_ */
