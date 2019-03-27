/*
 * Copyright (c) 2014-2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PT_PACKET_DECODER_H
#define PT_PACKET_DECODER_H

#include "intel-pt.h"


/* An Intel PT packet decoder. */
struct pt_packet_decoder {
	/* The decoder configuration. */
	struct pt_config config;

	/* The current position in the trace buffer. */
	const uint8_t *pos;

	/* The position of the last PSB packet. */
	const uint8_t *sync;
};


/* Initialize the packet decoder.
 *
 * Returns zero on success, a negative error code otherwise.
 */
extern int pt_pkt_decoder_init(struct pt_packet_decoder *,
			       const struct pt_config *);

/* Finalize the packet decoder. */
extern void pt_pkt_decoder_fini(struct pt_packet_decoder *);


/* Decoder functions for the packet decoder. */
extern int pt_pkt_decode_unknown(struct pt_packet_decoder *,
				 struct pt_packet *);
extern int pt_pkt_decode_pad(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_psb(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_tip(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_tnt_8(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_tnt_64(struct pt_packet_decoder *,
				struct pt_packet *);
extern int pt_pkt_decode_tip_pge(struct pt_packet_decoder *,
				 struct pt_packet *);
extern int pt_pkt_decode_tip_pgd(struct pt_packet_decoder *,
				 struct pt_packet *);
extern int pt_pkt_decode_fup(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_pip(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_ovf(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_mode(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_psbend(struct pt_packet_decoder *,
				struct pt_packet *);
extern int pt_pkt_decode_tsc(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_cbr(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_tma(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_mtc(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_cyc(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_stop(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_vmcs(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_mnt(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_exstop(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_mwait(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_pwre(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_pwrx(struct pt_packet_decoder *, struct pt_packet *);
extern int pt_pkt_decode_ptw(struct pt_packet_decoder *, struct pt_packet *);

#endif /* PT_PACKET_DECODER_H */
