/*
 * Point to Point Protocol (PPP) RFC1331
 *
 * Copyright 1989 by Carnegie Mellon.
 *
 * Permission to use, copy, modify, and distribute this program for any
 * purpose and without fee is hereby granted, provided that this copyright
 * and permission notice appear on all copies and supporting documentation,
 * the name of Carnegie Mellon not be used in advertising or publicity
 * pertaining to distribution of the program without specific prior
 * permission, and notice be given in supporting documentation that copying
 * and distribution is by permission of Carnegie Mellon and Stanford
 * University.  Carnegie Mellon makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 */
#define PPP_ADDRESS	0xff	/* The address byte value */
#define PPP_CONTROL	0x03	/* The control byte value */

#define PPP_PPPD_IN	0x00	/* non-standard for DLT_PPP_PPPD */
#define PPP_PPPD_OUT	0x01	/* non-standard for DLT_PPP_PPPD */

/* Protocol numbers */
#define PPP_IP		0x0021	/* Raw IP */
#define PPP_OSI		0x0023	/* OSI Network Layer */
#define PPP_NS		0x0025	/* Xerox NS IDP */
#define PPP_DECNET	0x0027	/* DECnet Phase IV */
#define PPP_APPLE	0x0029	/* Appletalk */
#define PPP_IPX		0x002b	/* Novell IPX */
#define PPP_VJC		0x002d	/* Van Jacobson Compressed TCP/IP */
#define PPP_VJNC	0x002f	/* Van Jacobson Uncompressed TCP/IP */
#define PPP_BRPDU	0x0031	/* Bridging PDU */
#define PPP_STII	0x0033	/* Stream Protocol (ST-II) */
#define PPP_VINES	0x0035	/* Banyan Vines */
#define PPP_IPV6	0x0057	/* Internet Protocol version 6 */

#define PPP_HELLO	0x0201	/* 802.1d Hello Packets */
#define PPP_LUXCOM	0x0231	/* Luxcom */
#define PPP_SNS		0x0233	/* Sigma Network Systems */
#define PPP_MPLS_UCAST  0x0281  /* rfc 3032 */
#define PPP_MPLS_MCAST  0x0283  /* rfc 3022 */

#define PPP_IPCP	0x8021	/* IP Control Protocol */
#define PPP_OSICP	0x8023	/* OSI Network Layer Control Protocol */
#define PPP_NSCP	0x8025	/* Xerox NS IDP Control Protocol */
#define PPP_DECNETCP	0x8027	/* DECnet Control Protocol */
#define PPP_APPLECP	0x8029	/* Appletalk Control Protocol */
#define PPP_IPXCP	0x802b	/* Novell IPX Control Protocol */
#define PPP_STIICP	0x8033	/* Strean Protocol Control Protocol */
#define PPP_VINESCP	0x8035	/* Banyan Vines Control Protocol */
#define PPP_IPV6CP	0x8057	/* IPv6 Control Protocol */
#define PPP_MPLSCP      0x8281  /* rfc 3022 */

#define PPP_LCP		0xc021	/* Link Control Protocol */
#define PPP_PAP		0xc023	/* Password Authentication Protocol */
#define PPP_LQM		0xc025	/* Link Quality Monitoring */
#define PPP_CHAP	0xc223	/* Challenge Handshake Authentication Protocol */
