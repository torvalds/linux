/*-
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from the Stanford/CMU enet packet filter,
 * (net/enet.c) distributed as part of 4.3BSD, and code contributed
 * to Berkeley by Steven McCanne and Van Jacobson both of Lawrence
 * Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * For captures on Linux cooked sockets, we construct a fake header
 * that includes:
 *
 *	a 2-byte "packet type" which is one of:
 *
 *		LINUX_SLL_HOST		packet was sent to us
 *		LINUX_SLL_BROADCAST	packet was broadcast
 *		LINUX_SLL_MULTICAST	packet was multicast
 *		LINUX_SLL_OTHERHOST	packet was sent to somebody else
 *		LINUX_SLL_OUTGOING	packet was sent *by* us;
 *
 *	a 2-byte Ethernet protocol field;
 *
 *	a 2-byte link-layer type;
 *
 *	a 2-byte link-layer address length;
 *
 *	an 8-byte source link-layer address, whose actual length is
 *	specified by the previous value.
 *
 * All fields except for the link-layer address are in network byte order.
 *
 * DO NOT change the layout of this structure, or change any of the
 * LINUX_SLL_ values below.  If you must change the link-layer header
 * for a "cooked" Linux capture, introduce a new DLT_ type (ask
 * "tcpdump-workers@lists.tcpdump.org" for one, so that you don't give it
 * a value that collides with a value already being used), and use the
 * new header in captures of that type, so that programs that can
 * handle DLT_LINUX_SLL captures will continue to handle them correctly
 * without any change, and so that capture files with different headers
 * can be told apart and programs that read them can dissect the
 * packets in them.
 */

#ifndef lib_pcap_sll_h
#define lib_pcap_sll_h

/*
 * A DLT_LINUX_SLL fake link-layer header.
 */
#define SLL_HDR_LEN	16		/* total header length */
#define SLL_ADDRLEN	8		/* length of address field */

#include <pcap/pcap-inttypes.h>

struct sll_header {
	uint16_t sll_pkttype;		/* packet type */
	uint16_t sll_hatype;		/* link-layer address type */
	uint16_t sll_halen;		/* link-layer address length */
	uint8_t sll_addr[SLL_ADDRLEN];	/* link-layer address */
	uint16_t sll_protocol;		/* protocol */
};

/*
 * The LINUX_SLL_ values for "sll_pkttype"; these correspond to the
 * PACKET_ values on Linux, but are defined here so that they're
 * available even on systems other than Linux, and so that they
 * don't change even if the PACKET_ values change.
 */
#define LINUX_SLL_HOST		0
#define LINUX_SLL_BROADCAST	1
#define LINUX_SLL_MULTICAST	2
#define LINUX_SLL_OTHERHOST	3
#define LINUX_SLL_OUTGOING	4

/*
 * The LINUX_SLL_ values for "sll_protocol"; these correspond to the
 * ETH_P_ values on Linux, but are defined here so that they're
 * available even on systems other than Linux.  We assume, for now,
 * that the ETH_P_ values won't change in Linux; if they do, then:
 *
 *	if we don't translate them in "pcap-linux.c", capture files
 *	won't necessarily be readable if captured on a system that
 *	defines ETH_P_ values that don't match these values;
 *
 *	if we do translate them in "pcap-linux.c", that makes life
 *	unpleasant for the BPF code generator, as the values you test
 *	for in the kernel aren't the values that you test for when
 *	reading a capture file, so the fixup code run on BPF programs
 *	handed to the kernel ends up having to do more work.
 *
 * Add other values here as necessary, for handling packet types that
 * might show up on non-Ethernet, non-802.x networks.  (Not all the ones
 * in the Linux "if_ether.h" will, I suspect, actually show up in
 * captures.)
 */
#define LINUX_SLL_P_802_3	0x0001	/* Novell 802.3 frames without 802.2 LLC header */
#define LINUX_SLL_P_802_2	0x0004	/* 802.2 frames (not D/I/X Ethernet) */
#define LINUX_SLL_P_CAN		0x000C	/* CAN frames, with SocketCAN pseudo-headers */
#define LINUX_SLL_P_CANFD	0x000D	/* CAN FD frames, with SocketCAN pseudo-headers */

#endif
