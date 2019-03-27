/*
 * Copyright (c) 2013 The TCPDUMP project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* \summary: ZeroMQ Message Transport Protocol (ZMTP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

static const char tstr[] = " [|zmtp1]";

/* Maximum number of ZMTP/1.0 frame body bytes (without the flags) to dump in
 * hex and ASCII under a single "-v" flag.
 */
#define VBYTES 128

/*
 * Below is an excerpt from the "13/ZMTP" specification:
 *
 * A ZMTP message consists of 1 or more frames.
 *
 * A ZMTP frame consists of a length, followed by a flags field and a frame
 * body of (length - 1) octets. Note: the length includes the flags field, so
 * an empty frame has a length of 1.
 *
 * For frames with a length of 1 to 254 octets, the length SHOULD BE encoded
 * as a single octet. The minimum valid length of a frame is 1 octet, thus a
 * length of 0 is invalid and such frames SHOULD be discarded silently.
 *
 * For frames with lengths of 255 and greater, the length SHALL BE encoded as
 * a single octet with the value 255, followed by the length encoded as a
 * 64-bit unsigned integer in network byte order. For frames with lengths of
 * 1 to 254 octets this encoding MAY be also used.
 *
 * The flags field consists of a single octet containing various control
 * flags. Bit 0 is the least significant bit.
 *
 * - Bit 0 (MORE): More frames to follow. A value of 0 indicates that there
 *   are no more frames to follow. A value of 1 indicates that more frames
 *   will follow. On messages consisting of a single frame the MORE flag MUST
 *   be 0.
 *
 * - Bits 1-7: Reserved. Bits 1-7 are reserved for future use and SHOULD be
 *   zero.
 */

static const u_char *
zmtp1_print_frame(netdissect_options *ndo, const u_char *cp, const u_char *ep)
{
	uint64_t body_len_declared, body_len_captured, header_len;
	uint8_t flags;

	ND_PRINT((ndo, "\n\t"));
	ND_TCHECK2(*cp, 1); /* length/0xFF */

	if (cp[0] != 0xFF) {
		header_len = 1; /* length */
		body_len_declared = cp[0];
		ND_PRINT((ndo, " frame flags+body  (8-bit) length %" PRIu64, body_len_declared));
	} else {
		header_len = 1 + 8; /* 0xFF, length */
		ND_PRINT((ndo, " frame flags+body (64-bit) length"));
		ND_TCHECK2(*cp, header_len); /* 0xFF, length */
		body_len_declared = EXTRACT_64BITS(cp + 1);
		ND_PRINT((ndo, " %" PRIu64, body_len_declared));
	}
	if (body_len_declared == 0)
		return cp + header_len; /* skip to the next frame */
	ND_TCHECK2(*cp, header_len + 1); /* ..., flags */
	flags = cp[header_len];

	body_len_captured = ep - cp - header_len;
	if (body_len_declared > body_len_captured)
		ND_PRINT((ndo, " (%" PRIu64 " captured)", body_len_captured));
	ND_PRINT((ndo, ", flags 0x%02x", flags));

	if (ndo->ndo_vflag) {
		uint64_t body_len_printed = min(body_len_captured, body_len_declared);

		ND_PRINT((ndo, " (%s|%s|%s|%s|%s|%s|%s|%s)",
			flags & 0x80 ? "MBZ" : "-",
			flags & 0x40 ? "MBZ" : "-",
			flags & 0x20 ? "MBZ" : "-",
			flags & 0x10 ? "MBZ" : "-",
			flags & 0x08 ? "MBZ" : "-",
			flags & 0x04 ? "MBZ" : "-",
			flags & 0x02 ? "MBZ" : "-",
			flags & 0x01 ? "MORE" : "-"));

		if (ndo->ndo_vflag == 1)
			body_len_printed = min(VBYTES + 1, body_len_printed);
		if (body_len_printed > 1) {
			ND_PRINT((ndo, ", first %" PRIu64 " byte(s) of body:", body_len_printed - 1));
			hex_and_ascii_print(ndo, "\n\t ", cp + header_len + 1, body_len_printed - 1);
			ND_PRINT((ndo, "\n"));
		}
	}

	/*
	 * Do not advance cp by the sum of header_len and body_len_declared
	 * before each offset has successfully passed ND_TCHECK2() as the
	 * sum can roll over (9 + 0xfffffffffffffff7 = 0) and cause an
	 * infinite loop.
	 */
	cp += header_len;
	ND_TCHECK2(*cp, body_len_declared); /* Next frame within the buffer ? */
	return cp + body_len_declared;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return ep;
}

void
zmtp1_print(netdissect_options *ndo, const u_char *cp, u_int len)
{
	const u_char *ep = min(ndo->ndo_snapend, cp + len);

	ND_PRINT((ndo, ": ZMTP/1.0"));
	while (cp < ep)
		cp = zmtp1_print_frame(ndo, cp, ep);
}

/* The functions below decode a ZeroMQ datagram, supposedly stored in the "Data"
 * field of an ODATA/RDATA [E]PGM packet. An excerpt from zmq_pgm(7) man page
 * follows.
 *
 * In order for late joining consumers to be able to identify message
 * boundaries, each PGM datagram payload starts with a 16-bit unsigned integer
 * in network byte order specifying either the offset of the first message frame
 * in the datagram or containing the value 0xFFFF if the datagram contains
 * solely an intermediate part of a larger message.
 *
 * Note that offset specifies where the first message begins rather than the
 * first message part. Thus, if there are trailing message parts at the
 * beginning of the packet the offset ignores them and points to first initial
 * message part in the packet.
 */

static const u_char *
zmtp1_print_intermediate_part(netdissect_options *ndo, const u_char *cp, const u_int len)
{
	u_int frame_offset;
	uint64_t remaining_len;

	ND_TCHECK2(*cp, 2);
	frame_offset = EXTRACT_16BITS(cp);
	ND_PRINT((ndo, "\n\t frame offset 0x%04x", frame_offset));
	cp += 2;
	remaining_len = ndo->ndo_snapend - cp; /* without the frame length */

	if (frame_offset == 0xFFFF)
		frame_offset = len - 2; /* always within the declared length */
	else if (2 + frame_offset > len) {
		ND_PRINT((ndo, " (exceeds datagram declared length)"));
		goto trunc;
	}

	/* offset within declared length of the datagram */
	if (frame_offset) {
		ND_PRINT((ndo, "\n\t frame intermediate part, %u bytes", frame_offset));
		if (frame_offset > remaining_len)
			ND_PRINT((ndo, " (%"PRIu64" captured)", remaining_len));
		if (ndo->ndo_vflag) {
			uint64_t len_printed = min(frame_offset, remaining_len);

			if (ndo->ndo_vflag == 1)
				len_printed = min(VBYTES, len_printed);
			if (len_printed > 1) {
				ND_PRINT((ndo, ", first %"PRIu64" byte(s):", len_printed));
				hex_and_ascii_print(ndo, "\n\t ", cp, len_printed);
				ND_PRINT((ndo, "\n"));
			}
		}
	}
	return cp + frame_offset;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return cp + len;
}

void
zmtp1_print_datagram(netdissect_options *ndo, const u_char *cp, const u_int len)
{
	const u_char *ep = min(ndo->ndo_snapend, cp + len);

	cp = zmtp1_print_intermediate_part(ndo, cp, len);
	while (cp < ep)
		cp = zmtp1_print_frame(ndo, cp, ep);
}
