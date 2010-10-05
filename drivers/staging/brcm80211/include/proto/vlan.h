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

#ifndef _vlan_h_
#define _vlan_h_

#include <typedefs.h>
#include <packed_section_start.h>

#define VLAN_VID_MASK		0xfff
#define	VLAN_CFI_SHIFT		12
#define VLAN_PRI_SHIFT		13

#define VLAN_PRI_MASK		7

#define	VLAN_TAG_LEN		4
#define	VLAN_TAG_OFFSET		(2 * ETHER_ADDR_LEN)

#define VLAN_TPID		0x8100

struct ethervlan_header {
	uint8 ether_dhost[ETHER_ADDR_LEN];
	uint8 ether_shost[ETHER_ADDR_LEN];
	uint16 vlan_type;
	uint16 vlan_tag;
	uint16 ether_type;
};

#define	ETHERVLAN_HDR_LEN	(ETHER_HDR_LEN + VLAN_TAG_LEN)

#include <packed_section_end.h>

#endif				/* _vlan_h_ */
