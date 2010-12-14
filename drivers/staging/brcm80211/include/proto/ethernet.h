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

#include <linux/if_ether.h>

#include <packed_section_start.h>

#define	ETHER_TYPE_LEN		2
#define	ETHER_CRC_LEN		4
#define	ETHER_MIN_LEN		64
#define	ETHER_MIN_DATA		46
#define	ETHER_MAX_LEN		1518
#define	ETHER_MAX_DATA		1500

#define	ETHER_TYPE_BRCM		0x886c

#define ETHER_DEST_OFFSET	(0 * ETH_ALEN)
#define ETHER_SRC_OFFSET	(1 * ETH_ALEN)
#define ETHER_TYPE_OFFSET	(2 * ETH_ALEN)

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
	u8 ether_dhost[ETH_ALEN];
	u8 ether_shost[ETH_ALEN];
	u16 ether_type;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct ether_addr {
	u8 octet[ETH_ALEN];
} BWL_POST_PACKED_STRUCT;

#define ETHER_SET_UNICAST(ea)	(((u8 *)(ea))[0] = (((u8 *)(ea))[0] & ~1))

static const struct ether_addr ether_bcast = { {255, 255, 255, 255, 255, 255} };

#define ETHER_MOVE_HDR(d, s) \
do { \
	struct ether_header t; \
	t = *(struct ether_header *)(s); \
	*(struct ether_header *)(d) = t; \
} while (0)

#include <packed_section_end.h>

#endif				/* _NET_ETHERNET_H_ */
