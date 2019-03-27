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

#ifndef PT_ENCODER_H
#define PT_ENCODER_H

#include "intel-pt.h"


/* An Intel PT packet encoder. */
struct pt_encoder {
	/* The encoder configuration. */
	struct pt_config config;

	/** The current position in the trace buffer. */
	uint8_t *pos;
};


/* Initialize the packet encoder.
 *
 * Returns zero on success, a negative error code otherwise.
 */
extern int pt_encoder_init(struct pt_encoder *, const struct pt_config *);

/* Finalize the packet encoder. */
extern void pt_encoder_fini(struct pt_encoder *);


/* The below functions are convenience wrappers around pt_enc_next(). */

/* Encode a Padding (pad) packet. */
extern int pt_encode_pad(struct pt_encoder *);

/* Encode a Packet Stream Boundary (psb) packet. */
extern int pt_encode_psb(struct pt_encoder *);

/* Encode an End PSB (psbend) packet. */
extern int pt_encode_psbend(struct pt_encoder *);

/* Encode a Target Instruction Pointer (tip) packet. */
extern int pt_encode_tip(struct pt_encoder *, uint64_t ip,
			 enum pt_ip_compression ipc);

/* Encode a Taken Not Taken (tnt) packet - 8-bit version. */
extern int pt_encode_tnt_8(struct pt_encoder *, uint8_t tnt, int size);

/* Encode a Taken Not Taken (tnt) packet - 64-bit version. */
extern int pt_encode_tnt_64(struct pt_encoder *, uint64_t tnt, int size);

/* Encode a Packet Generation Enable (tip.pge) packet. */
extern int pt_encode_tip_pge(struct pt_encoder *, uint64_t ip,
			     enum pt_ip_compression ipc);

/* Encode a Packet Generation Disable (tip.pgd) packet. */
extern int pt_encode_tip_pgd(struct pt_encoder *, uint64_t ip,
			     enum pt_ip_compression ipc);

/* Encode a Flow Update Packet (fup). */
extern int pt_encode_fup(struct pt_encoder *, uint64_t ip,
			 enum pt_ip_compression ipc);

/* Encode a Paging Information Packet (pip). */
extern int pt_encode_pip(struct pt_encoder *, uint64_t cr3, uint8_t flags);

/* Encode a Overflow Packet (ovf). */
extern int pt_encode_ovf(struct pt_encoder *);

/* Encode a Mode Exec Packet (mode.exec). */
extern int pt_encode_mode_exec(struct pt_encoder *, enum pt_exec_mode);

/* Encode a Mode Tsx Packet (mode.tsx). */
extern int pt_encode_mode_tsx(struct pt_encoder *, uint8_t);

/* Encode a Time Stamp Counter (tsc) packet. */
extern int pt_encode_tsc(struct pt_encoder *, uint64_t);

/* Encode a Core Bus Ratio (cbr) packet. */
extern int pt_encode_cbr(struct pt_encoder *, uint8_t);

/* Encode a TSC/MTC Alignment (tma) packet. */
extern int pt_encode_tma(struct pt_encoder *, uint16_t ctc,
				   uint16_t fc);

/* Encode a Mini Time Counter (mtc) packet. */
extern int pt_encode_mtc(struct pt_encoder *, uint8_t ctc);

/* Encode a Cycle Count (cyc) packet. */
extern int pt_encode_cyc(struct pt_encoder *, uint32_t cyc);

/* Encode a TraceStop Packet (stop). */
extern int pt_encode_stop(struct pt_encoder *);

/* Encode a VMCS packet. */
extern int pt_encode_vmcs(struct pt_encoder *, uint64_t);

/* Encode a Maintenance (mnt) packet. */
extern int pt_encode_mnt(struct pt_encoder *, uint64_t);

#endif /* PT_ENCODER_H */
