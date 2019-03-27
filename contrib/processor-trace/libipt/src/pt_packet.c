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

#include "pt_packet.h"
#include "pt_opcodes.h"

#include "intel-pt.h"

#include <limits.h>


static uint64_t pt_pkt_read_value(const uint8_t *pos, int size)
{
	uint64_t val;
	int idx;

	for (val = 0, idx = 0; idx < size; ++idx) {
		uint64_t byte = *pos++;

		byte <<= (idx * 8);
		val |= byte;
	}

	return val;
}

int pt_pkt_read_unknown(struct pt_packet *packet, const uint8_t *pos,
			const struct pt_config *config)
{
	int (*decode)(struct pt_packet_unknown *, const struct pt_config *,
		      const uint8_t *, void *);
	int size;

	if (!packet || !pos || !config)
		return -pte_internal;

	decode = config->decode.callback;
	if (!decode)
		return -pte_bad_opc;

	/* Fill in some default values. */
	packet->payload.unknown.packet = pos;
	packet->payload.unknown.priv = NULL;

	/* We accept a size of zero to allow the callback to modify the
	 * trace buffer and resume normal decoding.
	 */
	size = (*decode)(&packet->payload.unknown, config, pos,
			 config->decode.context);
	if (size < 0)
		return size;

	if (size > UCHAR_MAX)
		return -pte_invalid;

	packet->type = ppt_unknown;
	packet->size = (uint8_t) size;

	if (config->end < pos + size)
		return -pte_eos;

	return size;
}

int pt_pkt_read_psb(const uint8_t *pos, const struct pt_config *config)
{
	int count;

	if (!pos || !config)
		return -pte_internal;

	if (config->end < pos + ptps_psb)
		return -pte_eos;

	pos += pt_opcs_psb;

	for (count = 0; count < pt_psb_repeat_count; ++count) {
		if (*pos++ != pt_psb_hi)
			return -pte_bad_packet;
		if (*pos++ != pt_psb_lo)
			return -pte_bad_packet;
	}

	return ptps_psb;
}

static int pt_pkt_ip_size(enum pt_ip_compression ipc)
{
	switch (ipc) {
	case pt_ipc_suppressed:
		return 0;

	case pt_ipc_update_16:
		return 2;

	case pt_ipc_update_32:
		return 4;

	case pt_ipc_update_48:
	case pt_ipc_sext_48:
		return 6;

	case pt_ipc_full:
		return 8;
	}

	return -pte_bad_packet;
}

int pt_pkt_read_ip(struct pt_packet_ip *packet, const uint8_t *pos,
		   const struct pt_config *config)
{
	uint64_t ip;
	uint8_t ipc;
	int ipsize;

	if (!packet || !pos || !config)
		return -pte_internal;

	ipc = (*pos++ >> pt_opm_ipc_shr) & pt_opm_ipc_shr_mask;

	ip = 0ull;
	ipsize = pt_pkt_ip_size((enum pt_ip_compression) ipc);
	if (ipsize < 0)
		return ipsize;

	if (config->end < pos + ipsize)
		return -pte_eos;

	if (ipsize)
		ip = pt_pkt_read_value(pos, ipsize);

	packet->ipc = (enum pt_ip_compression) ipc;
	packet->ip = ip;

	return ipsize + 1;
}

static uint8_t pt_pkt_tnt_bit_size(uint64_t payload)
{
	uint8_t size;

	/* The payload bit-size is the bit-index of the payload's stop-bit,
	 * which itself is not part of the payload proper.
	 */
	for (size = 0; ; size += 1) {
		payload >>= 1;
		if (!payload)
			break;
	}

	return size;
}

static int pt_pkt_read_tnt(struct pt_packet_tnt *packet, uint64_t payload)
{
	uint8_t bit_size;

	if (!packet)
		return -pte_internal;

	bit_size = pt_pkt_tnt_bit_size(payload);
	if (!bit_size)
		return -pte_bad_packet;

	/* Remove the stop bit from the payload. */
	payload &= ~(1ull << bit_size);

	packet->payload = payload;
	packet->bit_size = bit_size;

	return 0;
}

int pt_pkt_read_tnt_8(struct pt_packet_tnt *packet, const uint8_t *pos,
		      const struct pt_config *config)
{
	int errcode;

	(void) config;

	if (!pos)
		return -pte_internal;

	errcode = pt_pkt_read_tnt(packet, pos[0] >> pt_opm_tnt_8_shr);
	if (errcode < 0)
		return errcode;

	return ptps_tnt_8;
}

int pt_pkt_read_tnt_64(struct pt_packet_tnt *packet, const uint8_t *pos,
		       const struct pt_config *config)
{
	uint64_t payload;
	int errcode;

	if (!pos || !config)
		return -pte_internal;

	if (config->end < pos + ptps_tnt_64)
		return -pte_eos;

	payload = pt_pkt_read_value(pos + pt_opcs_tnt_64, pt_pl_tnt_64_size);

	errcode = pt_pkt_read_tnt(packet, payload);
	if (errcode < 0)
		return errcode;

	return ptps_tnt_64;
}

int pt_pkt_read_pip(struct pt_packet_pip *packet, const uint8_t *pos,
		    const struct pt_config *config)
{
	uint64_t payload;

	if (!packet || !pos || !config)
		return -pte_internal;

	if (config->end < pos + ptps_pip)
		return -pte_eos;

	/* Read the payload. */
	payload = pt_pkt_read_value(pos + pt_opcs_pip, pt_pl_pip_size);

	/* Extract the non-root information from the payload. */
	packet->nr = payload & pt_pl_pip_nr;

	/* Create the cr3 value. */
	payload  >>= pt_pl_pip_shr;
	payload  <<= pt_pl_pip_shl;
	packet->cr3 = payload;

	return ptps_pip;
}

static int pt_pkt_read_mode_exec(struct pt_packet_mode_exec *packet,
				 uint8_t mode)
{
	if (!packet)
		return -pte_internal;

	packet->csl = (mode & pt_mob_exec_csl) != 0;
	packet->csd = (mode & pt_mob_exec_csd) != 0;

	return ptps_mode;
}

static int pt_pkt_read_mode_tsx(struct pt_packet_mode_tsx *packet,
				uint8_t mode)
{
	if (!packet)
		return -pte_internal;

	packet->intx = (mode & pt_mob_tsx_intx) != 0;
	packet->abrt = (mode & pt_mob_tsx_abrt) != 0;

	return ptps_mode;
}

int pt_pkt_read_mode(struct pt_packet_mode *packet, const uint8_t *pos,
		     const struct pt_config *config)
{
	uint8_t payload, mode, leaf;

	if (!packet || !pos || !config)
		return -pte_internal;

	if (config->end < pos + ptps_mode)
		return -pte_eos;

	payload = pos[pt_opcs_mode];
	leaf = payload & pt_mom_leaf;
	mode = payload & pt_mom_bits;

	packet->leaf = (enum pt_mode_leaf) leaf;
	switch (leaf) {
	default:
		return -pte_bad_packet;

	case pt_mol_exec:
		return pt_pkt_read_mode_exec(&packet->bits.exec, mode);

	case pt_mol_tsx:
		return pt_pkt_read_mode_tsx(&packet->bits.tsx, mode);
	}
}

int pt_pkt_read_tsc(struct pt_packet_tsc *packet, const uint8_t *pos,
		    const struct pt_config *config)
{
	if (!packet || !pos || !config)
		return -pte_internal;

	if (config->end < pos + ptps_tsc)
		return -pte_eos;

	packet->tsc = pt_pkt_read_value(pos + pt_opcs_tsc, pt_pl_tsc_size);

	return ptps_tsc;
}

int pt_pkt_read_cbr(struct pt_packet_cbr *packet, const uint8_t *pos,
		    const struct pt_config *config)
{
	if (!packet || !pos || !config)
		return -pte_internal;

	if (config->end < pos + ptps_cbr)
		return -pte_eos;

	packet->ratio = pos[2];

	return ptps_cbr;
}

int pt_pkt_read_tma(struct pt_packet_tma *packet, const uint8_t *pos,
		    const struct pt_config *config)
{
	uint16_t ctc, fc;

	if (!packet || !pos || !config)
		return -pte_internal;

	if (config->end < pos + ptps_tma)
		return -pte_eos;

	ctc = pos[pt_pl_tma_ctc_0];
	ctc |= pos[pt_pl_tma_ctc_1] << 8;

	fc = pos[pt_pl_tma_fc_0];
	fc |= pos[pt_pl_tma_fc_1] << 8;

	if (fc & ~pt_pl_tma_fc_mask)
		return -pte_bad_packet;

	packet->ctc = ctc;
	packet->fc = fc;

	return ptps_tma;
}

int pt_pkt_read_mtc(struct pt_packet_mtc *packet, const uint8_t *pos,
		    const struct pt_config *config)
{
	if (!packet || !pos || !config)
		return -pte_internal;

	if (config->end < pos + ptps_mtc)
		return -pte_eos;

	packet->ctc = pos[pt_opcs_mtc];

	return ptps_mtc;
}

int pt_pkt_read_cyc(struct pt_packet_cyc *packet, const uint8_t *pos,
		    const struct pt_config *config)
{
	const uint8_t *begin, *end;
	uint64_t value;
	uint8_t cyc, ext, shl;

	if (!packet || !pos || !config)
		return -pte_internal;

	begin = pos;
	end = config->end;

	/* The first byte contains the opcode and part of the payload.
	 * We already checked that this first byte is within bounds.
	 */
	cyc = *pos++;

	ext = cyc & pt_opm_cyc_ext;
	cyc >>= pt_opm_cyc_shr;

	value = cyc;
	shl = (8 - pt_opm_cyc_shr);

	while (ext) {
		uint64_t bits;

		if (end <= pos)
			return -pte_eos;

		bits = *pos++;
		ext = bits & pt_opm_cycx_ext;

		bits >>= pt_opm_cycx_shr;
		bits <<= shl;

		shl += (8 - pt_opm_cycx_shr);
		if (sizeof(value) * 8 < shl)
			return -pte_bad_packet;

		value |= bits;
	}

	packet->value = value;

	return (int) (pos - begin);
}

int pt_pkt_read_vmcs(struct pt_packet_vmcs *packet, const uint8_t *pos,
		     const struct pt_config *config)
{
	uint64_t payload;

	if (!packet || !pos || !config)
		return -pte_internal;

	if (config->end < pos + ptps_vmcs)
		return -pte_eos;

	payload = pt_pkt_read_value(pos + pt_opcs_vmcs, pt_pl_vmcs_size);

	packet->base = payload << pt_pl_vmcs_shl;

	return ptps_vmcs;
}

int pt_pkt_read_mnt(struct pt_packet_mnt *packet, const uint8_t *pos,
		    const struct pt_config *config)
{
	if (!packet || !pos || !config)
		return -pte_internal;

	if (config->end < pos + ptps_mnt)
		return -pte_eos;

	packet->payload = pt_pkt_read_value(pos + pt_opcs_mnt, pt_pl_mnt_size);

	return ptps_mnt;
}

int pt_pkt_read_exstop(struct pt_packet_exstop *packet, const uint8_t *pos,
		       const struct pt_config *config)
{
	if (!packet || !pos || !config)
		return -pte_internal;

	if (config->end < pos + ptps_exstop)
		return -pte_eos;

	packet->ip = pos[1] & pt_pl_exstop_ip_mask ? 1 : 0;

	return ptps_exstop;
}

int pt_pkt_read_mwait(struct pt_packet_mwait *packet, const uint8_t *pos,
		      const struct pt_config *config)
{
	if (!packet || !pos || !config)
		return -pte_internal;

	if (config->end < pos + ptps_mwait)
		return -pte_eos;

	packet->hints = (uint32_t) pt_pkt_read_value(pos + pt_opcs_mwait,
						     pt_pl_mwait_hints_size);
	packet->ext = (uint32_t) pt_pkt_read_value(pos + pt_opcs_mwait +
						   pt_pl_mwait_hints_size,
						   pt_pl_mwait_ext_size);
	return ptps_mwait;
}

int pt_pkt_read_pwre(struct pt_packet_pwre *packet, const uint8_t *pos,
		     const struct pt_config *config)
{
	uint64_t payload;

	if (!packet || !pos || !config)
		return -pte_internal;

	if (config->end < pos + ptps_pwre)
		return -pte_eos;

	payload = pt_pkt_read_value(pos + pt_opcs_pwre, pt_pl_pwre_size);

	memset(packet, 0, sizeof(*packet));
	packet->state = (uint8_t) ((payload & pt_pl_pwre_state_mask) >>
				   pt_pl_pwre_state_shr);
	packet->sub_state = (uint8_t) ((payload & pt_pl_pwre_sub_state_mask) >>
				       pt_pl_pwre_sub_state_shr);
	if (payload & pt_pl_pwre_hw_mask)
		packet->hw = 1;

	return ptps_pwre;
}

int pt_pkt_read_pwrx(struct pt_packet_pwrx *packet, const uint8_t *pos,
		     const struct pt_config *config)
{
	uint64_t payload;

	if (!packet || !pos || !config)
		return -pte_internal;

	if (config->end < pos + ptps_pwrx)
		return -pte_eos;

	payload = pt_pkt_read_value(pos + pt_opcs_pwrx, pt_pl_pwrx_size);

	memset(packet, 0, sizeof(*packet));
	packet->last = (uint8_t) ((payload & pt_pl_pwrx_last_mask) >>
				  pt_pl_pwrx_last_shr);
	packet->deepest = (uint8_t) ((payload & pt_pl_pwrx_deepest_mask) >>
				     pt_pl_pwrx_deepest_shr);
	if (payload & pt_pl_pwrx_wr_int)
		packet->interrupt = 1;
	if (payload & pt_pl_pwrx_wr_store)
		packet->store = 1;
	if (payload & pt_pl_pwrx_wr_hw)
		packet->autonomous = 1;

	return ptps_pwrx;
}

int pt_pkt_read_ptw(struct pt_packet_ptw *packet, const uint8_t *pos,
		    const struct pt_config *config)
{
	uint8_t opc, plc;
	int size;

	if (!packet || !pos || !config)
		return -pte_internal;

	/* Skip the ext opcode. */
	pos++;

	opc = *pos++;
	plc = (opc >> pt_opm_ptw_pb_shr) & pt_opm_ptw_pb_shr_mask;

	size = pt_ptw_size(plc);
	if (size < 0)
		return size;

	if (config->end < pos + size)
		return -pte_eos;

	packet->payload = pt_pkt_read_value(pos, size);
	packet->plc = plc;
	packet->ip = opc & pt_opm_ptw_ip ? 1 : 0;

	return pt_opcs_ptw + size;
}
