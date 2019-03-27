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

#ifndef PT_PACKET_H
#define PT_PACKET_H

#include <stdint.h>

struct pt_config;
struct pt_packet;
struct pt_packet_ip;
struct pt_packet_tnt;
struct pt_packet_pip;
struct pt_packet_mode;
struct pt_packet_tsc;
struct pt_packet_cbr;
struct pt_packet_tma;
struct pt_packet_mtc;
struct pt_packet_cyc;
struct pt_packet_vmcs;
struct pt_packet_mnt;
struct pt_packet_exstop;
struct pt_packet_mwait;
struct pt_packet_pwre;
struct pt_packet_pwrx;
struct pt_packet_ptw;


/* Read the payload of an Intel PT packet.
 *
 * Reads the payload of the packet starting at @pos into @packet.
 *
 * For pt_pkt_read_psb(), the @packet parameter is omitted; the function
 * validates that the payload matches the expected PSB pattern.
 *
 * Decoding an unknown packet uses @config's decode callback.  If the callback
 * is NULL, pt_pkt_read_unknown() returns -pte_bad_opc.
 *
 * Beware that the packet opcode is not checked.  The caller is responsible
 * for checking the opcode and calling the correct packet read function.
 *
 * Returns the packet size on success, a negative error code otherwise.
 * Returns -pte_bad_packet if the packet payload is corrupt.
 * Returns -pte_eos if the packet does not fit into the trace buffer.
 * Returns -pte_internal if @packet, @pos, or @config is NULL.
 */
extern int pt_pkt_read_unknown(struct pt_packet *packet, const uint8_t *pos,
			       const struct pt_config *config);
extern int pt_pkt_read_psb(const uint8_t *pos, const struct pt_config *config);
extern int pt_pkt_read_ip(struct pt_packet_ip *packet, const uint8_t *pos,
			  const struct pt_config *config);
extern int pt_pkt_read_tnt_8(struct pt_packet_tnt *packet, const uint8_t *pos,
			     const struct pt_config *config);
extern int pt_pkt_read_tnt_64(struct pt_packet_tnt *packet, const uint8_t *pos,
			      const struct pt_config *config);
extern int pt_pkt_read_pip(struct pt_packet_pip *packet, const uint8_t *pos,
			   const struct pt_config *config);
extern int pt_pkt_read_mode(struct pt_packet_mode *packet, const uint8_t *pos,
			    const struct pt_config *config);
extern int pt_pkt_read_tsc(struct pt_packet_tsc *packet, const uint8_t *pos,
			   const struct pt_config *config);
extern int pt_pkt_read_cbr(struct pt_packet_cbr *packet, const uint8_t *pos,
			   const struct pt_config *config);
extern int pt_pkt_read_tma(struct pt_packet_tma *packet, const uint8_t *pos,
			   const struct pt_config *config);
extern int pt_pkt_read_mtc(struct pt_packet_mtc *packet, const uint8_t *pos,
			   const struct pt_config *config);
extern int pt_pkt_read_cyc(struct pt_packet_cyc *packet, const uint8_t *pos,
			   const struct pt_config *config);
extern int pt_pkt_read_vmcs(struct pt_packet_vmcs *packet, const uint8_t *pos,
			    const struct pt_config *config);
extern int pt_pkt_read_mnt(struct pt_packet_mnt *packet, const uint8_t *pos,
			   const struct pt_config *config);
extern int pt_pkt_read_exstop(struct pt_packet_exstop *packet,
			      const uint8_t *pos,
			      const struct pt_config *config);
extern int pt_pkt_read_mwait(struct pt_packet_mwait *packet, const uint8_t *pos,
			     const struct pt_config *config);
extern int pt_pkt_read_pwre(struct pt_packet_pwre *packet, const uint8_t *pos,
			    const struct pt_config *config);
extern int pt_pkt_read_pwrx(struct pt_packet_pwrx *packet, const uint8_t *pos,
			    const struct pt_config *config);
extern int pt_pkt_read_ptw(struct pt_packet_ptw *packet, const uint8_t *pos,
			   const struct pt_config *config);

#endif /* PT_PACKET_H */
