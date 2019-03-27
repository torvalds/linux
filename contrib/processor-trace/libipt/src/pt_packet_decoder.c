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

#include "pt_packet_decoder.h"
#include "pt_decoder_function.h"
#include "pt_packet.h"
#include "pt_sync.h"
#include "pt_config.h"
#include "pt_opcodes.h"

#include <string.h>
#include <stdlib.h>


int pt_pkt_decoder_init(struct pt_packet_decoder *decoder,
			const struct pt_config *config)
{
	int errcode;

	if (!decoder || !config)
		return -pte_invalid;

	memset(decoder, 0, sizeof(*decoder));

	errcode = pt_config_from_user(&decoder->config, config);
	if (errcode < 0)
		return errcode;

	return 0;
}

struct pt_packet_decoder *pt_pkt_alloc_decoder(const struct pt_config *config)
{
	struct pt_packet_decoder *decoder;
	int errcode;

	decoder = malloc(sizeof(*decoder));
	if (!decoder)
		return NULL;

	errcode = pt_pkt_decoder_init(decoder, config);
	if (errcode < 0) {
		free(decoder);
		return NULL;
	}

	return decoder;
}

void pt_pkt_decoder_fini(struct pt_packet_decoder *decoder)
{
	(void) decoder;

	/* Nothing to do. */
}

void pt_pkt_free_decoder(struct pt_packet_decoder *decoder)
{
	pt_pkt_decoder_fini(decoder);
	free(decoder);
}

int pt_pkt_sync_forward(struct pt_packet_decoder *decoder)
{
	const uint8_t *pos, *sync;
	int errcode;

	if (!decoder)
		return -pte_invalid;

	sync = decoder->sync;
	pos = decoder->pos;
	if (!pos)
		pos = decoder->config.begin;

	if (pos == sync)
		pos += ptps_psb;

	errcode = pt_sync_forward(&sync, pos, &decoder->config);
	if (errcode < 0)
		return errcode;

	decoder->sync = sync;
	decoder->pos = sync;

	return 0;
}

int pt_pkt_sync_backward(struct pt_packet_decoder *decoder)
{
	const uint8_t *pos, *sync;
	int errcode;

	if (!decoder)
		return -pte_invalid;

	pos = decoder->pos;
	if (!pos)
		pos = decoder->config.end;

	errcode = pt_sync_backward(&sync, pos, &decoder->config);
	if (errcode < 0)
		return errcode;

	decoder->sync = sync;
	decoder->pos = sync;

	return 0;
}

int pt_pkt_sync_set(struct pt_packet_decoder *decoder, uint64_t offset)
{
	const uint8_t *begin, *end, *pos;

	if (!decoder)
		return -pte_invalid;

	begin = decoder->config.begin;
	end = decoder->config.end;
	pos = begin + offset;

	if (end < pos || pos < begin)
		return -pte_eos;

	decoder->sync = pos;
	decoder->pos = pos;

	return 0;
}

int pt_pkt_get_offset(const struct pt_packet_decoder *decoder, uint64_t *offset)
{
	const uint8_t *begin, *pos;

	if (!decoder || !offset)
		return -pte_invalid;

	begin = decoder->config.begin;
	pos = decoder->pos;

	if (!pos)
		return -pte_nosync;

	*offset = pos - begin;
	return 0;
}

int pt_pkt_get_sync_offset(const struct pt_packet_decoder *decoder,
			   uint64_t *offset)
{
	const uint8_t *begin, *sync;

	if (!decoder || !offset)
		return -pte_invalid;

	begin = decoder->config.begin;
	sync = decoder->sync;

	if (!sync)
		return -pte_nosync;

	*offset = sync - begin;
	return 0;
}

const struct pt_config *
pt_pkt_get_config(const struct pt_packet_decoder *decoder)
{
	if (!decoder)
		return NULL;

	return &decoder->config;
}

static inline int pkt_to_user(struct pt_packet *upkt, size_t size,
			      const struct pt_packet *pkt)
{
	if (!upkt || !pkt)
		return -pte_internal;

	if (upkt == pkt)
		return 0;

	/* Zero out any unknown bytes. */
	if (sizeof(*pkt) < size) {
		memset(upkt + sizeof(*pkt), 0, size - sizeof(*pkt));

		size = sizeof(*pkt);
	}

	memcpy(upkt, pkt, size);

	return 0;
}

int pt_pkt_next(struct pt_packet_decoder *decoder, struct pt_packet *packet,
		size_t psize)
{
	const struct pt_decoder_function *dfun;
	struct pt_packet pkt, *ppkt;
	int errcode, size;

	if (!packet || !decoder)
		return -pte_invalid;

	ppkt = psize == sizeof(pkt) ? packet : &pkt;

	errcode = pt_df_fetch(&dfun, decoder->pos, &decoder->config);
	if (errcode < 0)
		return errcode;

	if (!dfun)
		return -pte_internal;

	if (!dfun->packet)
		return -pte_internal;

	size = dfun->packet(decoder, ppkt);
	if (size < 0)
		return size;

	errcode = pkt_to_user(packet, psize, ppkt);
	if (errcode < 0)
		return errcode;

	decoder->pos += size;

	return size;
}

int pt_pkt_decode_unknown(struct pt_packet_decoder *decoder,
			  struct pt_packet *packet)
{
	int size;

	if (!decoder)
		return -pte_internal;

	size = pt_pkt_read_unknown(packet, decoder->pos, &decoder->config);
	if (size < 0)
		return size;

	return size;
}

int pt_pkt_decode_pad(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	(void) decoder;

	if (!packet)
		return -pte_internal;

	packet->type = ppt_pad;
	packet->size = ptps_pad;

	return ptps_pad;
}

int pt_pkt_decode_psb(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder)
		return -pte_internal;

	size = pt_pkt_read_psb(decoder->pos, &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_psb;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_tip(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_ip(&packet->payload.ip, decoder->pos,
			      &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_tip;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_tnt_8(struct pt_packet_decoder *decoder,
			struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_tnt_8(&packet->payload.tnt, decoder->pos,
				 &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_tnt_8;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_tnt_64(struct pt_packet_decoder *decoder,
			 struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_tnt_64(&packet->payload.tnt, decoder->pos,
				  &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_tnt_64;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_tip_pge(struct pt_packet_decoder *decoder,
			  struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_ip(&packet->payload.ip, decoder->pos,
			      &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_tip_pge;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_tip_pgd(struct pt_packet_decoder *decoder,
			  struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_ip(&packet->payload.ip, decoder->pos,
			      &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_tip_pgd;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_fup(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_ip(&packet->payload.ip, decoder->pos,
			      &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_fup;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_pip(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_pip(&packet->payload.pip, decoder->pos,
			       &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_pip;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_ovf(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	(void) decoder;

	if (!packet)
		return -pte_internal;

	packet->type = ppt_ovf;
	packet->size = ptps_ovf;

	return ptps_ovf;
}

int pt_pkt_decode_mode(struct pt_packet_decoder *decoder,
		       struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_mode(&packet->payload.mode, decoder->pos,
				&decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_mode;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_psbend(struct pt_packet_decoder *decoder,
			 struct pt_packet *packet)
{
	(void) decoder;

	if (!packet)
		return -pte_internal;

	packet->type = ppt_psbend;
	packet->size = ptps_psbend;

	return ptps_psbend;
}

int pt_pkt_decode_tsc(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_tsc(&packet->payload.tsc, decoder->pos,
			       &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_tsc;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_cbr(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_cbr(&packet->payload.cbr, decoder->pos,
			       &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_cbr;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_tma(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_tma(&packet->payload.tma, decoder->pos,
			       &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_tma;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_mtc(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_mtc(&packet->payload.mtc, decoder->pos,
			       &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_mtc;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_cyc(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_cyc(&packet->payload.cyc, decoder->pos,
			       &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_cyc;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_stop(struct pt_packet_decoder *decoder,
		       struct pt_packet *packet)
{
	(void) decoder;

	if (!packet)
		return -pte_internal;

	packet->type = ppt_stop;
	packet->size = ptps_stop;

	return ptps_stop;
}

int pt_pkt_decode_vmcs(struct pt_packet_decoder *decoder,
		       struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_vmcs(&packet->payload.vmcs, decoder->pos,
			       &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_vmcs;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_mnt(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_mnt(&packet->payload.mnt, decoder->pos,
			       &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_mnt;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_exstop(struct pt_packet_decoder *decoder,
			 struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_exstop(&packet->payload.exstop, decoder->pos,
				  &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_exstop;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_mwait(struct pt_packet_decoder *decoder,
			struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_mwait(&packet->payload.mwait, decoder->pos,
				 &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_mwait;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_pwre(struct pt_packet_decoder *decoder,
		       struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_pwre(&packet->payload.pwre, decoder->pos,
				&decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_pwre;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_pwrx(struct pt_packet_decoder *decoder,
		       struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_pwrx(&packet->payload.pwrx, decoder->pos,
				&decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_pwrx;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_ptw(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_ptw(&packet->payload.ptw, decoder->pos,
			       &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_ptw;
	packet->size = (uint8_t) size;

	return size;
}
