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

#ifndef PT_QUERY_DECODER_H
#define PT_QUERY_DECODER_H

#include "pt_last_ip.h"
#include "pt_tnt_cache.h"
#include "pt_time.h"
#include "pt_event_queue.h"

#include "intel-pt.h"

struct pt_decoder_function;


/* An Intel PT query decoder. */
struct pt_query_decoder {
	/* The decoder configuration. */
	struct pt_config config;

	/* The current position in the trace buffer. */
	const uint8_t *pos;

	/* The position of the last PSB packet. */
	const uint8_t *sync;

	/* The decoding function for the next packet. */
	const struct pt_decoder_function *next;

	/* The last-ip. */
	struct pt_last_ip ip;

	/* The cached tnt indicators. */
	struct pt_tnt_cache tnt;

	/* Timing information. */
	struct pt_time time;

	/* The time at the last query (before reading ahead). */
	struct pt_time last_time;

	/* Timing calibration. */
	struct pt_time_cal tcal;

	/* Pending (incomplete) events. */
	struct pt_event_queue evq;

	/* The current event. */
	struct pt_event *event;

	/* A collection of flags relevant for decoding:
	 *
	 * - tracing is enabled.
	 */
	uint32_t enabled:1;

	/* - consume the current packet. */
	uint32_t consume_packet:1;
};

/* Initialize the query decoder.
 *
 * Returns zero on success, a negative error code otherwise.
 */
extern int pt_qry_decoder_init(struct pt_query_decoder *,
			       const struct pt_config *);

/* Finalize the query decoder. */
extern void pt_qry_decoder_fini(struct pt_query_decoder *);

/* Decoder functions (tracing context). */
extern int pt_qry_decode_unknown(struct pt_query_decoder *);
extern int pt_qry_decode_pad(struct pt_query_decoder *);
extern int pt_qry_decode_psb(struct pt_query_decoder *);
extern int pt_qry_decode_tip(struct pt_query_decoder *);
extern int pt_qry_decode_tnt_8(struct pt_query_decoder *);
extern int pt_qry_decode_tnt_64(struct pt_query_decoder *);
extern int pt_qry_decode_tip_pge(struct pt_query_decoder *);
extern int pt_qry_decode_tip_pgd(struct pt_query_decoder *);
extern int pt_qry_decode_fup(struct pt_query_decoder *);
extern int pt_qry_decode_pip(struct pt_query_decoder *);
extern int pt_qry_decode_ovf(struct pt_query_decoder *);
extern int pt_qry_decode_mode(struct pt_query_decoder *);
extern int pt_qry_decode_psbend(struct pt_query_decoder *);
extern int pt_qry_decode_tsc(struct pt_query_decoder *);
extern int pt_qry_header_tsc(struct pt_query_decoder *);
extern int pt_qry_decode_cbr(struct pt_query_decoder *);
extern int pt_qry_header_cbr(struct pt_query_decoder *);
extern int pt_qry_decode_tma(struct pt_query_decoder *);
extern int pt_qry_decode_mtc(struct pt_query_decoder *);
extern int pt_qry_decode_cyc(struct pt_query_decoder *);
extern int pt_qry_decode_stop(struct pt_query_decoder *);
extern int pt_qry_decode_vmcs(struct pt_query_decoder *);
extern int pt_qry_decode_mnt(struct pt_query_decoder *);
extern int pt_qry_decode_exstop(struct pt_query_decoder *);
extern int pt_qry_decode_mwait(struct pt_query_decoder *);
extern int pt_qry_decode_pwre(struct pt_query_decoder *);
extern int pt_qry_decode_pwrx(struct pt_query_decoder *);
extern int pt_qry_decode_ptw(struct pt_query_decoder *);

/* Decoder functions (header context). */
extern int pt_qry_header_fup(struct pt_query_decoder *);
extern int pt_qry_header_pip(struct pt_query_decoder *);
extern int pt_qry_header_mode(struct pt_query_decoder *);
extern int pt_qry_header_vmcs(struct pt_query_decoder *);
extern int pt_qry_header_mnt(struct pt_query_decoder *);

#endif /* PT_QUERY_DECODER_H */
