/*-
 * Copyright (c) 2008 Apple Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE. 
 */

#ifndef _BSM_AUDIT_DOMAIN_H_
#define	_BSM_AUDIT_DOMAIN_H_

/*
 * BSM protocol domain constants - protocol domains defined in Solaris.
 */
#define	BSM_PF_UNSPEC		0
#define	BSM_PF_LOCAL		1
#define	BSM_PF_INET		2
#define	BSM_PF_IMPLINK		3
#define	BSM_PF_PUP		4
#define	BSM_PF_CHAOS		5
#define	BSM_PF_NS		6
#define	BSM_PF_NBS		7	/* Solaris-specific. */
#define	BSM_PF_ECMA		8
#define	BSM_PF_DATAKIT		9
#define	BSM_PF_CCITT		10
#define	BSM_PF_SNA		11
#define	BSM_PF_DECnet		12
#define	BSM_PF_DLI		13
#define	BSM_PF_LAT		14
#define	BSM_PF_HYLINK		15
#define	BSM_PF_APPLETALK	16
#define	BSM_PF_NIT		17	/* Solaris-specific. */
#define	BSM_PF_802		18	/* Solaris-specific. */
#define	BSM_PF_OSI		19
#define	BSM_PF_X25		20	/* Solaris/Linux-specific. */
#define	BSM_PF_OSINET		21	/* Solaris-specific. */
#define	BSM_PF_GOSIP		22	/* Solaris-specific. */
#define	BSM_PF_IPX		23
#define	BSM_PF_ROUTE		24
#define	BSM_PF_LINK		25
#define	BSM_PF_INET6		26
#define	BSM_PF_KEY		27
#define	BSM_PF_NCA		28	/* Solaris-specific. */
#define	BSM_PF_POLICY		29	/* Solaris-specific. */
#define	BSM_PF_INET_OFFLOAD	30	/* Solaris-specific. */

/*
 * BSM protocol domain constants - protocol domains not defined in Solaris.
 */
#define	BSM_PF_NETBIOS		500	/* FreeBSD/Darwin-specific. */
#define	BSM_PF_ISO		501	/* FreeBSD/Darwin-specific. */
#define	BSM_PF_XTP		502	/* FreeBSD/Darwin-specific. */
#define	BSM_PF_COIP		503	/* FreeBSD/Darwin-specific. */
#define	BSM_PF_CNT		504	/* FreeBSD/Darwin-specific. */
#define	BSM_PF_RTIP		505	/* FreeBSD/Darwin-specific. */
#define	BSM_PF_SIP		506	/* FreeBSD/Darwin-specific. */
#define	BSM_PF_PIP		507	/* FreeBSD/Darwin-specific. */
#define	BSM_PF_ISDN		508	/* FreeBSD/Darwin-specific. */
#define	BSM_PF_E164		509	/* FreeBSD/Darwin-specific. */
#define	BSM_PF_NATM		510	/* FreeBSD/Darwin-specific. */
#define	BSM_PF_ATM		511	/* FreeBSD/Darwin-specific. */
#define	BSM_PF_NETGRAPH		512	/* FreeBSD/Darwin-specific. */
#define	BSM_PF_SLOW		513	/* FreeBSD-specific. */
#define	BSM_PF_SCLUSTER		514	/* FreeBSD-specific. */
#define	BSM_PF_ARP		515	/* FreeBSD-specific. */
#define	BSM_PF_BLUETOOTH	516	/* FreeBSD-specific. */
					/* 517: unallocated. */
#define	BSM_PF_AX25		518	/* Linux-specific. */
#define	BSM_PF_ROSE		519	/* Linux-specific. */
#define	BSM_PF_NETBEUI		520	/* Linux-specific. */
#define	BSM_PF_SECURITY		521	/* Linux-specific. */
#define	BSM_PF_PACKET		522	/* Linux-specific. */
#define	BSM_PF_ASH		523	/* Linux-specific. */
#define	BSM_PF_ECONET		524	/* Linux-specific. */
#define	BSM_PF_ATMSVC		525	/* Linux-specific. */
#define	BSM_PF_IRDA		526	/* Linux-specific. */
#define	BSM_PF_PPPOX		527	/* Linux-specific. */
#define	BSM_PF_WANPIPE		528	/* Linux-specific. */
#define	BSM_PF_LLC		529	/* Linux-specific. */
#define	BSM_PF_CAN		530	/* Linux-specific. */
#define	BSM_PF_TIPC		531	/* Linux-specific. */
#define	BSM_PF_IUCV		532	/* Linux-specific. */
#define	BSM_PF_RXRPC		533	/* Linux-specific. */
#define	BSM_PF_PHONET		534	/* Linux-specific. */

/*
 * Used when there is no mapping from a local to BSM protocol domain.
 */
#define	BSM_PF_UNKNOWN		700	/* OpenBSM-specific. */

#endif /* !_BSM_AUDIT_DOMAIN_H_ */
