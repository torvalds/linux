/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * ppp_defs.h - PPP definitions.
 *
 * Copyright 1994-2000 Paul Mackerras.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 */
#include <linux/types.h>

#ifndef _PPP_DEFS_H_
#define _PPP_DEFS_H_

/*
 * The basic PPP frame.
 */
#define PPP_HDRLEN	4	/* octets for standard ppp header */
#define PPP_FCSLEN	2	/* octets for FCS */
#define PPP_MRU		1500	/* default MRU = max length of info field */

#define PPP_ADDRESS(p)	(((__u8 *)(p))[0])
#define PPP_CONTROL(p)	(((__u8 *)(p))[1])
#define PPP_PROTOCOL(p)	((((__u8 *)(p))[2] << 8) + ((__u8 *)(p))[3])

/*
 * Significant octet values.
 */
#define	PPP_ALLSTATIONS	0xff	/* All-Stations broadcast address */
#define	PPP_UI		0x03	/* Unnumbered Information */
#define	PPP_FLAG	0x7e	/* Flag Sequence */
#define	PPP_ESCAPE	0x7d	/* Asynchronous Control Escape */
#define	PPP_TRANS	0x20	/* Asynchronous transparency modifier */

/*
 * Protocol field values.
 */
#define PPP_IP		0x21	/* Internet Protocol */
#define PPP_AT		0x29	/* AppleTalk Protocol */
#define PPP_IPX		0x2b	/* IPX protocol */
#define	PPP_VJC_COMP	0x2d	/* VJ compressed TCP */
#define	PPP_VJC_UNCOMP	0x2f	/* VJ uncompressed TCP */
#define PPP_MP		0x3d	/* Multilink protocol */
#define PPP_IPV6	0x57	/* Internet Protocol Version 6 */
#define PPP_COMPFRAG	0xfb	/* fragment compressed below bundle */
#define PPP_COMP	0xfd	/* compressed packet */
#define PPP_MPLS_UC	0x0281	/* Multi Protocol Label Switching - Unicast */
#define PPP_MPLS_MC	0x0283	/* Multi Protocol Label Switching - Multicast */
#define PPP_IPCP	0x8021	/* IP Control Protocol */
#define PPP_ATCP	0x8029	/* AppleTalk Control Protocol */
#define PPP_IPXCP	0x802b	/* IPX Control Protocol */
#define PPP_IPV6CP	0x8057	/* IPv6 Control Protocol */
#define PPP_CCPFRAG	0x80fb	/* CCP at link level (below MP bundle) */
#define PPP_CCP		0x80fd	/* Compression Control Protocol */
#define PPP_MPLSCP	0x80fd	/* MPLS Control Protocol */
#define PPP_LCP		0xc021	/* Link Control Protocol */
#define PPP_PAP		0xc023	/* Password Authentication Protocol */
#define PPP_LQR		0xc025	/* Link Quality Report protocol */
#define PPP_CHAP	0xc223	/* Cryptographic Handshake Auth. Protocol */
#define PPP_CBCP	0xc029	/* Callback Control Protocol */

/*
 * Values for FCS calculations.
 */

#define PPP_INITFCS	0xffff	/* Initial FCS value */
#define PPP_GOODFCS	0xf0b8	/* Good final FCS value */


/*
 * Extended asyncmap - allows any character to be escaped.
 */

typedef __u32		ext_accm[8];

/*
 * What to do with network protocol (NP) packets.
 */
enum NPmode {
    NPMODE_PASS,		/* pass the packet through */
    NPMODE_DROP,		/* silently drop the packet */
    NPMODE_ERROR,		/* return an error */
    NPMODE_QUEUE		/* save it up for later. */
};

/*
 * Statistics for LQRP and pppstats
 */
struct pppstat	{
    __u32	ppp_discards;	/* # frames discarded */

    __u32	ppp_ibytes;	/* bytes received */
    __u32	ppp_ioctects;	/* bytes received not in error */
    __u32	ppp_ipackets;	/* packets received */
    __u32	ppp_ierrors;	/* receive errors */
    __u32	ppp_ilqrs;	/* # LQR frames received */

    __u32	ppp_obytes;	/* raw bytes sent */
    __u32	ppp_ooctects;	/* frame bytes sent */
    __u32	ppp_opackets;	/* packets sent */
    __u32	ppp_oerrors;	/* transmit errors */ 
    __u32	ppp_olqrs;	/* # LQR frames sent */
};

struct vjstat {
    __u32	vjs_packets;	/* outbound packets */
    __u32	vjs_compressed;	/* outbound compressed packets */
    __u32	vjs_searches;	/* searches for connection state */
    __u32	vjs_misses;	/* times couldn't find conn. state */
    __u32	vjs_uncompressedin; /* inbound uncompressed packets */
    __u32	vjs_compressedin;   /* inbound compressed packets */
    __u32	vjs_errorin;	/* inbound unknown type packets */
    __u32	vjs_tossed;	/* inbound packets tossed because of error */
};

struct compstat {
    __u32	unc_bytes;	/* total uncompressed bytes */
    __u32	unc_packets;	/* total uncompressed packets */
    __u32	comp_bytes;	/* compressed bytes */
    __u32	comp_packets;	/* compressed packets */
    __u32	inc_bytes;	/* incompressible bytes */
    __u32	inc_packets;	/* incompressible packets */

    /* the compression ratio is defined as in_count / bytes_out */
    __u32       in_count;	/* Bytes received */
    __u32       bytes_out;	/* Bytes transmitted */

    double	ratio;		/* not computed in kernel. */
};

struct ppp_stats {
    struct pppstat	p;	/* basic PPP statistics */
    struct vjstat	vj;	/* VJ header compression statistics */
};

struct ppp_comp_stats {
    struct compstat	c;	/* packet compression statistics */
    struct compstat	d;	/* packet decompression statistics */
};

/*
 * The following structure records the time in seconds since
 * the last NP packet was sent or received.
 *
 * Linux implements both 32-bit and 64-bit time_t versions
 * for compatibility with user space that defines ppp_idle
 * based on the libc time_t.
 */
struct ppp_idle {
    __kernel_old_time_t xmit_idle;	/* time since last NP packet sent */
    __kernel_old_time_t recv_idle;	/* time since last NP packet received */
};

struct ppp_idle32 {
    __s32 xmit_idle;		/* time since last NP packet sent */
    __s32 recv_idle;		/* time since last NP packet received */
};

struct ppp_idle64 {
    __s64 xmit_idle;		/* time since last NP packet sent */
    __s64 recv_idle;		/* time since last NP packet received */
};

#endif /* _PPP_DEFS_H_ */
