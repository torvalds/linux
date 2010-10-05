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

#ifndef _BCMETH_H_
#define _BCMETH_H_

#include <typedefs.h>
#include <packed_section_start.h>

#define	BCMILCP_SUBTYPE_RATE		1
#define	BCMILCP_SUBTYPE_LINK		2
#define	BCMILCP_SUBTYPE_CSA		3
#define	BCMILCP_SUBTYPE_LARQ		4
#define BCMILCP_SUBTYPE_VENDOR		5
#define	BCMILCP_SUBTYPE_FLH		17
#define BCMILCP_SUBTYPE_VENDOR_LONG	32769
#define BCMILCP_SUBTYPE_CERT		32770
#define BCMILCP_SUBTYPE_SES		32771
#define BCMILCP_BCM_SUBTYPE_RESERVED		0
#define BCMILCP_BCM_SUBTYPE_EVENT		1
#define BCMILCP_BCM_SUBTYPE_SES			2
#define BCMILCP_BCM_SUBTYPE_DPT			4
#define BCMILCP_BCM_SUBTYPEHDR_MINLENGTH	8
#define BCMILCP_BCM_SUBTYPEHDR_VERSION		0

typedef BWL_PRE_PACKED_STRUCT struct bcmeth_hdr {
	uint16 subtype;
	uint16 length;
	u8 version;
	u8 oui[3];
	uint16 usr_subtype;
} BWL_POST_PACKED_STRUCT bcmeth_hdr_t;

#include <packed_section_end.h>

#endif				/* _BCMETH_H_ */
