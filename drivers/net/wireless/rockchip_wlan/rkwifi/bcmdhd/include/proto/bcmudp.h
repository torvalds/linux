/*
 * Fundamental constants relating to UDP Protocol
 *
 * Copyright (C) 2016, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 *
 * $Id: bcmudp.h 518342 2014-12-01 23:21:41Z $
 */

#ifndef _bcmudp_h_
#define _bcmudp_h_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>


/* UDP header */
#define UDP_DEST_PORT_OFFSET	2	/* UDP dest port offset */
#define UDP_LEN_OFFSET		4	/* UDP length offset */
#define UDP_CHKSUM_OFFSET	6	/* UDP body checksum offset */

#define UDP_HDR_LEN	8	/* UDP header length */
#define UDP_PORT_LEN	2	/* UDP port length */

/* These fields are stored in network order */
BWL_PRE_PACKED_STRUCT struct bcmudp_hdr
{
	uint16	src_port;	/* Source Port Address */
	uint16	dst_port;	/* Destination Port Address */
	uint16	len;		/* Number of bytes in datagram including header */
	uint16	chksum;		/* entire datagram checksum with pseudoheader */
} BWL_POST_PACKED_STRUCT;

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif	/* #ifndef _bcmudp_h_ */
