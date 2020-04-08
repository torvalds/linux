/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Fundamental constants relating to ICMP Protocol
 *
 * Copyright (C) 1999-2019, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: bcmicmp.h 700076 2017-05-17 14:42:22Z $
 */

#ifndef _bcmicmp_h_
#define _bcmicmp_h_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif // endif

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

#define ICMP_TYPE_ECHO_REQUEST	8	/* ICMP type echo request */
#define ICMP_TYPE_ECHO_REPLY		0	/* ICMP type echo reply */

#define ICMP_CHKSUM_OFFSET	2	/* ICMP body checksum offset */

/* ICMP6 error and control message types */
#define ICMP6_DEST_UNREACHABLE		1
#define ICMP6_PKT_TOO_BIG		2
#define ICMP6_TIME_EXCEEDED		3
#define ICMP6_PARAM_PROBLEM		4
#define ICMP6_ECHO_REQUEST		128
#define ICMP6_ECHO_REPLY		129
#define ICMP_MCAST_LISTENER_QUERY	130
#define ICMP_MCAST_LISTENER_REPORT	131
#define ICMP_MCAST_LISTENER_DONE	132
#define ICMP6_RTR_SOLICITATION		133
#define ICMP6_RTR_ADVERTISEMENT		134
#define ICMP6_NEIGH_SOLICITATION	135
#define ICMP6_NEIGH_ADVERTISEMENT	136
#define ICMP6_REDIRECT			137

#define ICMP6_RTRSOL_OPT_OFFSET		8
#define ICMP6_RTRADV_OPT_OFFSET		16
#define ICMP6_NEIGHSOL_OPT_OFFSET	24
#define ICMP6_NEIGHADV_OPT_OFFSET	24
#define ICMP6_REDIRECT_OPT_OFFSET	40

BWL_PRE_PACKED_STRUCT struct icmp6_opt {
	uint8	type;		/* Option identifier */
	uint8	length;		/* Lenth including type and length */
	uint8	data[0];	/* Variable length data */
} BWL_POST_PACKED_STRUCT;

#define	ICMP6_OPT_TYPE_SRC_LINK_LAYER	1
#define	ICMP6_OPT_TYPE_TGT_LINK_LAYER	2
#define	ICMP6_OPT_TYPE_PREFIX_INFO	3
#define	ICMP6_OPT_TYPE_REDIR_HDR	4
#define	ICMP6_OPT_TYPE_MTU		5

/* These fields are stored in network order */
BWL_PRE_PACKED_STRUCT struct bcmicmp_hdr {
	uint8	type;		/* Echo or Echo-reply */
	uint8	code;		/* Always 0 */
	uint16	chksum;		/* Icmp packet checksum */
} BWL_POST_PACKED_STRUCT;

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif	/* #ifndef _bcmicmp_h_ */
