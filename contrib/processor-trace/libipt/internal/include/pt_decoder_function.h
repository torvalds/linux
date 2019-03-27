/*
 * Copyright (c) 2013-2018, Intel Corporation
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

#ifndef PT_DECODER_FUNCTION_H
#define PT_DECODER_FUNCTION_H

#include <stdint.h>

struct pt_query_decoder;
struct pt_packet_decoder;
struct pt_packet;
struct pt_config;


/* Intel(R) Processor Trace decoder function flags. */
enum pt_decoder_function_flag {
	/* The decoded packet contains an unconditional branch destination. */
	pdff_tip	= 1 << 0,

	/* The decode packet contains unconditional branch destinations. */
	pdff_tnt	= 1 << 1,

	/* The decoded packet contains an event. */
	pdff_event	= 1 << 2,

	/* The decoded packet marks the end of a PSB header. */
	pdff_psbend	= 1 << 3,

	/* The decoded packet contains a non-branch IP update. */
	pdff_fup	= 1 << 4,

	/* The decoded packet is unknown to the decoder. */
	pdff_unknown	= 1 << 5,

	/* The decoded packet contains timing information. */
	pdff_timing	= 1 << 6,

	/* The decoded packet contains padding. */
	pdff_pad	= 1 << 7
};

/* An Intel(R) Processor Trace decoder function. */
struct pt_decoder_function {
	/* The function to analyze the next packet. */
	int (*packet)(struct pt_packet_decoder *, struct pt_packet *);

	/* The function to decode the next packet. */
	int (*decode)(struct pt_query_decoder *);

	/* The function to decode the next packet in segment header
	 * context, i.e. between PSB and ENDPSB.
	 */
	int (*header)(struct pt_query_decoder *);

	/* Decoder function flags. */
	int flags;
};


/* Fetch the decoder function.
 *
 * Sets @dfun to the decoder function for decoding the packet at @pos.
 *
 * Returns 0 on success.
 * Returns -pte_internal if @dfun or @config is NULL.
 * Returns -pte_nosync if @pos is NULL or outside @config's trace buffer.
 * Returns -pte_eos if the opcode is incomplete or missing.
 */
extern int pt_df_fetch(const struct pt_decoder_function **dfun,
		       const uint8_t *pos, const struct pt_config *config);


/* Decoder functions for the various packet types.
 *
 * Do not call those functions directly!
 */
extern const struct pt_decoder_function pt_decode_unknown;
extern const struct pt_decoder_function pt_decode_pad;
extern const struct pt_decoder_function pt_decode_psb;
extern const struct pt_decoder_function pt_decode_tip;
extern const struct pt_decoder_function pt_decode_tnt_8;
extern const struct pt_decoder_function pt_decode_tnt_64;
extern const struct pt_decoder_function pt_decode_tip_pge;
extern const struct pt_decoder_function pt_decode_tip_pgd;
extern const struct pt_decoder_function pt_decode_fup;
extern const struct pt_decoder_function pt_decode_pip;
extern const struct pt_decoder_function pt_decode_ovf;
extern const struct pt_decoder_function pt_decode_mode;
extern const struct pt_decoder_function pt_decode_psbend;
extern const struct pt_decoder_function pt_decode_tsc;
extern const struct pt_decoder_function pt_decode_cbr;
extern const struct pt_decoder_function pt_decode_tma;
extern const struct pt_decoder_function pt_decode_mtc;
extern const struct pt_decoder_function pt_decode_cyc;
extern const struct pt_decoder_function pt_decode_stop;
extern const struct pt_decoder_function pt_decode_vmcs;
extern const struct pt_decoder_function pt_decode_mnt;
extern const struct pt_decoder_function pt_decode_exstop;
extern const struct pt_decoder_function pt_decode_mwait;
extern const struct pt_decoder_function pt_decode_pwre;
extern const struct pt_decoder_function pt_decode_pwrx;
extern const struct pt_decoder_function pt_decode_ptw;

#endif /* PT_DECODER_FUNCTION_H */
