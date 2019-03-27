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

#include "ptunit.h"

#include "pt_last_ip.h"
#include "pt_decoder_function.h"
#include "pt_query_decoder.h"
#include "pt_encoder.h"
#include "pt_opcodes.h"


/* A query testing fixture. */

struct ptu_decoder_fixture {
	/* The test fixture initialization and finalization functions. */
	struct ptunit_result (*init)(struct ptu_decoder_fixture *);
	struct ptunit_result (*fini)(struct ptu_decoder_fixture *);

	/* Encode an optional header for the test to read over. */
	struct ptunit_result (*header)(struct ptu_decoder_fixture *);

	/* The trace buffer. */
	uint8_t buffer[1024];

	/* The configuration under test. */
	struct pt_config config;

	/* A encoder and query decoder for the above configuration. */
	struct pt_encoder encoder;
	struct pt_query_decoder decoder;

	/* For tracking last-ip in tests. */
	struct pt_last_ip last_ip;
};

/* An invalid address. */
static const uint64_t pt_dfix_bad_ip = (1ull << 62) - 1;

/* A sign-extended address. */
static const uint64_t pt_dfix_sext_ip = 0xffffff00ff00ff00ull;

/* The highest possible address. */
static const uint64_t pt_dfix_max_ip = (1ull << 47) - 1;

/* The highest possible cr3 value. */
static const uint64_t pt_dfix_max_cr3 = ((1ull << 47) - 1) & ~0x1f;

/* Synchronize the decoder at the beginning of the trace stream, avoiding the
 * initial PSB header.
 */
static struct ptunit_result ptu_sync_decoder(struct pt_query_decoder *decoder)
{
	ptu_ptr(decoder);
	decoder->enabled = 1;

	(void) pt_df_fetch(&decoder->next, decoder->pos, &decoder->config);
	return ptu_passed();
}

/* Cut off the last encoded packet. */
static struct ptunit_result cutoff(struct pt_query_decoder *decoder,
				   const struct pt_encoder *encoder)
{
	uint8_t *pos;

	ptu_ptr(decoder);
	ptu_ptr(encoder);

	pos = encoder->pos;
	ptu_ptr(pos);

	pos -= 1;
	ptu_ptr_le(decoder->config.begin, pos);

	decoder->config.end = pos;
	return ptu_passed();
}

static struct ptunit_result indir_not_synced(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	uint64_t ip = pt_dfix_bad_ip, addr = ip;
	int errcode;

	errcode = pt_qry_indirect_branch(decoder, &addr);
	ptu_int_eq(errcode, -pte_nosync);
	ptu_uint_eq(addr, ip);

	return ptu_passed();
}

static struct ptunit_result cond_not_synced(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	int errcode, tnt = 0xbc, taken = tnt;

	errcode = pt_qry_cond_branch(decoder, &taken);
	ptu_int_eq(errcode, -pte_nosync);
	ptu_int_eq(taken, tnt);

	return ptu_passed();
}

static struct ptunit_result event_not_synced(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_event event;
	int errcode;

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_nosync);

	return ptu_passed();
}

static struct ptunit_result sync_backward(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	uint64_t sync[3], offset, ip;
	int errcode;

	/* Check that we can use repeated pt_qry_sync_backward() to iterate over
	 * synchronization points in backwards order.
	 */

	errcode = pt_enc_get_offset(encoder, &sync[0]);
	ptu_int_ge(errcode, 0);

	pt_encode_psb(encoder);
	pt_encode_mode_exec(encoder, ptem_64bit);
	pt_encode_psbend(encoder);

	errcode = pt_enc_get_offset(encoder, &sync[1]);
	ptu_int_ge(errcode, 0);

	pt_encode_psb(encoder);
	pt_encode_mode_exec(encoder, ptem_64bit);
	pt_encode_psbend(encoder);

	errcode = pt_enc_get_offset(encoder, &sync[2]);
	ptu_int_ge(errcode, 0);

	pt_encode_psb(encoder);
	pt_encode_mode_exec(encoder, ptem_64bit);
	pt_encode_psbend(encoder);

	/* Synchronize repeatedly and check that we reach each PSB in the
	 * correct order.
	 */

	errcode = pt_qry_sync_backward(decoder, &ip);
	ptu_int_ge(errcode, 0);

	errcode = pt_qry_get_sync_offset(decoder, &offset);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(offset, sync[2]);

	errcode = pt_qry_sync_backward(decoder, &ip);
	ptu_int_ge(errcode, 0);

	errcode = pt_qry_get_sync_offset(decoder, &offset);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(offset, sync[1]);

	errcode = pt_qry_sync_backward(decoder, &ip);
	ptu_int_ge(errcode, 0);

	errcode = pt_qry_get_sync_offset(decoder, &offset);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(offset, sync[0]);

	errcode = pt_qry_sync_backward(decoder, &ip);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result
sync_backward_empty_end(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	uint64_t sync[3], offset, ip;
	int errcode;

	/* Check that we can use repeated pt_qry_sync_backward() to iterate over
	 * synchronization points in backwards order.
	 *
	 * There's an empty PSB+ at the end.  We skip it.
	 */

	errcode = pt_enc_get_offset(encoder, &sync[0]);
	ptu_int_ge(errcode, 0);

	pt_encode_psb(encoder);
	pt_encode_mode_exec(encoder, ptem_64bit);
	pt_encode_psbend(encoder);

	errcode = pt_enc_get_offset(encoder, &sync[1]);
	ptu_int_ge(errcode, 0);

	pt_encode_psb(encoder);
	pt_encode_mode_exec(encoder, ptem_64bit);
	pt_encode_psbend(encoder);

	errcode = pt_enc_get_offset(encoder, &sync[2]);
	ptu_int_ge(errcode, 0);

	pt_encode_psb(encoder);
	pt_encode_psbend(encoder);

	/* Synchronize repeatedly and check that we reach each PSB in the
	 * correct order.
	 */

	errcode = pt_qry_sync_backward(decoder, &ip);
	ptu_int_ge(errcode, 0);

	errcode = pt_qry_get_sync_offset(decoder, &offset);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(offset, sync[1]);

	errcode = pt_qry_sync_backward(decoder, &ip);
	ptu_int_ge(errcode, 0);

	errcode = pt_qry_get_sync_offset(decoder, &offset);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(offset, sync[0]);

	errcode = pt_qry_sync_backward(decoder, &ip);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result
sync_backward_empty_mid(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	uint64_t sync[3], offset, ip;
	int errcode;

	/* Check that we can use repeated pt_qry_sync_backward() to iterate over
	 * synchronization points in backwards order.
	 *
	 * There's an empty PSB+ in the middle.  We skip it.
	 */

	errcode = pt_enc_get_offset(encoder, &sync[0]);
	ptu_int_ge(errcode, 0);

	pt_encode_psb(encoder);
	pt_encode_mode_exec(encoder, ptem_64bit);
	pt_encode_psbend(encoder);

	errcode = pt_enc_get_offset(encoder, &sync[1]);
	ptu_int_ge(errcode, 0);

	pt_encode_psb(encoder);
	pt_encode_psbend(encoder);

	errcode = pt_enc_get_offset(encoder, &sync[2]);
	ptu_int_ge(errcode, 0);

	pt_encode_psb(encoder);
	pt_encode_mode_exec(encoder, ptem_64bit);
	pt_encode_psbend(encoder);

	/* Synchronize repeatedly and check that we reach each PSB in the
	 * correct order.
	 */

	errcode = pt_qry_sync_backward(decoder, &ip);
	ptu_int_ge(errcode, 0);

	errcode = pt_qry_get_sync_offset(decoder, &offset);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(offset, sync[2]);

	errcode = pt_qry_sync_backward(decoder, &ip);
	ptu_int_ge(errcode, 0);

	errcode = pt_qry_get_sync_offset(decoder, &offset);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(offset, sync[0]);

	errcode = pt_qry_sync_backward(decoder, &ip);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result
sync_backward_empty_begin(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	uint64_t sync[3], offset, ip;
	int errcode;

	/* Check that we can use repeated pt_qry_sync_backward() to iterate over
	 * synchronization points in backwards order.
	 *
	 * There's an empty PSB+ at the beginning.  We skip it.
	 */

	errcode = pt_enc_get_offset(encoder, &sync[0]);
	ptu_int_ge(errcode, 0);

	pt_encode_psb(encoder);
	pt_encode_psbend(encoder);

	errcode = pt_enc_get_offset(encoder, &sync[1]);
	ptu_int_ge(errcode, 0);

	pt_encode_psb(encoder);
	pt_encode_mode_exec(encoder, ptem_64bit);
	pt_encode_psbend(encoder);

	errcode = pt_enc_get_offset(encoder, &sync[2]);
	ptu_int_ge(errcode, 0);

	pt_encode_psb(encoder);
	pt_encode_mode_exec(encoder, ptem_64bit);
	pt_encode_psbend(encoder);

	/* Synchronize repeatedly and check that we reach each PSB in the
	 * correct order.
	 */

	errcode = pt_qry_sync_backward(decoder, &ip);
	ptu_int_ge(errcode, 0);

	errcode = pt_qry_get_sync_offset(decoder, &offset);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(offset, sync[2]);

	errcode = pt_qry_sync_backward(decoder, &ip);
	ptu_int_ge(errcode, 0);

	errcode = pt_qry_get_sync_offset(decoder, &offset);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(offset, sync[1]);

	errcode = pt_qry_sync_backward(decoder, &ip);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result
decode_sync_backward(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	uint64_t sync[2], offset, ip;
	int errcode;

	/* Check that we can use sync_backward to re-sync at the current trace
	 * segment as well as to find the previous trace segment.
	 */

	errcode = pt_enc_get_offset(encoder, &sync[0]);
	ptu_int_ge(errcode, 0);

	pt_encode_psb(encoder);
	pt_encode_mode_exec(encoder, ptem_64bit);
	pt_encode_psbend(encoder);

	errcode = pt_enc_get_offset(encoder, &sync[1]);
	ptu_int_ge(errcode, 0);

	pt_encode_psb(encoder);
	pt_encode_mode_exec(encoder, ptem_64bit);
	pt_encode_psbend(encoder);


	errcode = pt_qry_sync_forward(decoder, &ip);
	ptu_int_ge(errcode, 0);

	errcode = pt_qry_get_sync_offset(decoder, &offset);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(offset, sync[0]);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_ge(errcode, 0);
	ptu_int_eq(event.type, ptev_exec_mode);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_ge(errcode, 0);
	ptu_int_eq(event.type, ptev_exec_mode);

	errcode = pt_qry_sync_backward(decoder, &ip);
	ptu_int_ge(errcode, 0);

	errcode = pt_qry_get_sync_offset(decoder, &offset);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(offset, sync[1]);

	errcode = pt_qry_sync_backward(decoder, &ip);
	ptu_int_ge(errcode, 0);

	errcode = pt_qry_get_sync_offset(decoder, &offset);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(offset, sync[0]);

	errcode = pt_qry_sync_backward(decoder, &ip);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result indir_null(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_config *config = &decoder->config;
	uint64_t ip = pt_dfix_bad_ip, addr = ip;
	int errcode;

	errcode = pt_qry_indirect_branch(NULL, &addr);
	ptu_int_eq(errcode, -pte_invalid);
	ptu_uint_eq(addr, ip);

	errcode = pt_qry_indirect_branch(decoder, NULL);
	ptu_int_eq(errcode, -pte_invalid);
	ptu_ptr_eq(decoder->pos, config->begin);

	return ptu_passed();
}

static struct ptunit_result indir_empty(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_config *config = &decoder->config;
	uint64_t ip = pt_dfix_bad_ip, addr = ip;
	int errcode;

	decoder->pos = config->end;

	errcode = pt_qry_indirect_branch(decoder, &addr);
	ptu_int_eq(errcode, -pte_eos);
	ptu_uint_eq(addr, ip);

	return ptu_passed();
}

static struct ptunit_result indir(struct ptu_decoder_fixture *dfix,
				  enum pt_ip_compression ipc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_packet_ip packet;
	uint64_t addr = pt_dfix_bad_ip;
	int errcode;

	packet.ipc = ipc;
	packet.ip = pt_dfix_sext_ip;
	pt_last_ip_update_ip(&dfix->last_ip, &packet, &dfix->config);

	pt_encode_tip(encoder, packet.ip, packet.ipc);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_indirect_branch(decoder, &addr);
	if (ipc == pt_ipc_suppressed) {
		ptu_int_eq(errcode, pts_ip_suppressed | pts_eos);
		ptu_uint_eq(addr, pt_dfix_bad_ip);
	} else {
		ptu_int_eq(errcode, pts_eos);
		ptu_uint_eq(addr, dfix->last_ip.ip);
	}

	return ptu_passed();
}

static struct ptunit_result indir_tnt(struct ptu_decoder_fixture *dfix,
				      enum pt_ip_compression ipc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_packet_ip packet;
	uint64_t addr = pt_dfix_bad_ip;
	int errcode;

	packet.ipc = ipc;
	packet.ip = pt_dfix_sext_ip;
	pt_last_ip_update_ip(&dfix->last_ip, &packet, &dfix->config);

	pt_encode_tnt_8(encoder, 0ull, 1);
	pt_encode_tip(encoder, packet.ip, packet.ipc);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_indirect_branch(decoder, &addr);
	if (ipc == pt_ipc_suppressed) {
		ptu_int_eq(errcode, pts_ip_suppressed);
		ptu_uint_eq(addr, pt_dfix_bad_ip);
	} else {
		ptu_int_eq(errcode, 0);
		ptu_uint_eq(addr, dfix->last_ip.ip);
	}

	return ptu_passed();
}

static struct ptunit_result indir_cutoff_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	uint64_t ip = pt_dfix_bad_ip, addr = ip;
	int errcode;

	pt_encode_tip(encoder, 0, pt_ipc_sext_48);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_indirect_branch(decoder, &addr);
	ptu_int_eq(errcode, -pte_eos);
	ptu_uint_eq(addr, ip);

	return ptu_passed();
}

static struct ptunit_result
indir_skip_tnt_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	uint64_t ip = pt_dfix_bad_ip, addr = ip;
	int errcode;

	pt_encode_tnt_8(encoder, 0, 1);
	pt_encode_tnt_8(encoder, 0, 1);
	pt_encode_tip(encoder, 0, pt_ipc_sext_48);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_indirect_branch(decoder, &addr);
	ptu_int_eq(errcode, -pte_bad_query);
	ptu_uint_eq(addr, ip);

	return ptu_passed();
}

static struct ptunit_result
indir_skip_tip_pge_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	uint64_t ip = pt_dfix_bad_ip, addr = ip;
	const uint8_t *pos;
	int errcode;

	pos = encoder->pos;
	pt_encode_tip_pge(encoder, 0, pt_ipc_sext_48);
	pt_encode_tip(encoder, 0, pt_ipc_sext_48);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_indirect_branch(decoder, &addr);
	ptu_int_eq(errcode, -pte_bad_query);
	ptu_ptr_eq(decoder->pos, pos);
	ptu_uint_eq(addr, ip);

	return ptu_passed();
}

static struct ptunit_result
indir_skip_tip_pgd_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	uint64_t ip = pt_dfix_bad_ip, addr = ip;
	const uint8_t *pos;
	int errcode;

	pos = encoder->pos;
	pt_encode_tip_pgd(encoder, 0, pt_ipc_sext_48);
	pt_encode_tip(encoder, 0, pt_ipc_sext_48);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_indirect_branch(decoder, &addr);
	ptu_int_eq(errcode, -pte_bad_query);
	ptu_ptr_eq(decoder->pos, pos);
	ptu_uint_eq(addr, ip);

	return ptu_passed();
}

static struct ptunit_result
indir_skip_fup_tip_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	uint64_t ip = pt_dfix_bad_ip, addr = ip;
	const uint8_t *pos;
	int errcode;

	pt_encode_fup(encoder, 0, pt_ipc_sext_48);
	pos = encoder->pos;
	pt_encode_tip(encoder, 0, pt_ipc_sext_48);
	pt_encode_tip(encoder, 0, pt_ipc_sext_48);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_indirect_branch(decoder, &addr);
	ptu_int_eq(errcode, -pte_bad_query);
	ptu_ptr_eq(decoder->pos, pos);
	ptu_uint_eq(addr, ip);

	return ptu_passed();
}

static struct ptunit_result
indir_skip_fup_tip_pgd_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	uint64_t ip = pt_dfix_bad_ip, addr = ip;
	const uint8_t *pos;
	int errcode;

	pt_encode_fup(encoder, 0, pt_ipc_sext_48);
	pos = encoder->pos;
	pt_encode_tip_pgd(encoder, 0, pt_ipc_sext_48);
	pt_encode_tip(encoder, 0, pt_ipc_sext_48);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_indirect_branch(decoder, &addr);
	ptu_int_eq(errcode, -pte_bad_query);
	ptu_ptr_eq(decoder->pos, pos);
	ptu_uint_eq(addr, ip);

	return ptu_passed();
}

static struct ptunit_result cond_null(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_config *config = &decoder->config;
	int errcode, tnt = 0xbc, taken = tnt;

	errcode = pt_qry_cond_branch(NULL, &taken);
	ptu_int_eq(errcode, -pte_invalid);
	ptu_int_eq(taken, tnt);

	errcode = pt_qry_cond_branch(decoder, NULL);
	ptu_int_eq(errcode, -pte_invalid);
	ptu_ptr_eq(decoder->pos, config->begin);

	return ptu_passed();
}

static struct ptunit_result cond_empty(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_config *config = &decoder->config;
	int errcode, tnt = 0xbc, taken = tnt;

	decoder->pos = config->end;

	errcode = pt_qry_cond_branch(decoder, &taken);
	ptu_int_eq(errcode, -pte_eos);
	ptu_int_eq(taken, tnt);

	return ptu_passed();
}

static struct ptunit_result cond(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	int errcode, tnt = 0xbc, taken = tnt;

	pt_encode_tnt_8(encoder, 0x02, 3);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_cond_branch(decoder, &taken);
	ptu_int_eq(errcode, 0);
	ptu_int_eq(taken, 0);

	taken = tnt;
	errcode = pt_qry_cond_branch(decoder, &taken);
	ptu_int_eq(errcode, 0);
	ptu_int_eq(taken, 1);

	taken = tnt;
	errcode = pt_qry_cond_branch(decoder, &taken);
	ptu_int_eq(errcode, pts_eos);
	ptu_int_eq(taken, 0);

	taken = tnt;
	errcode = pt_qry_cond_branch(decoder, &taken);
	ptu_int_eq(errcode, -pte_eos);
	ptu_int_eq(taken, tnt);

	return ptu_passed();
}

static struct ptunit_result cond_skip_tip_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	int errcode, tnt = 0xbc, taken = tnt;
	const uint8_t *pos;

	pos = encoder->pos;
	pt_encode_tip(encoder, 0, pt_ipc_sext_48);
	pt_encode_tnt_8(encoder, 0, 1);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_cond_branch(decoder, &taken);
	ptu_int_eq(errcode, -pte_bad_query);
	ptu_ptr_eq(decoder->pos, pos);
	ptu_int_eq(taken, tnt);

	return ptu_passed();
}

static struct ptunit_result
cond_skip_tip_pge_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	int errcode, tnt = 0xbc, taken = tnt;
	const uint8_t *pos;

	pos = encoder->pos;
	pt_encode_tip_pge(encoder, 0, pt_ipc_sext_48);
	pt_encode_tnt_8(encoder, 0, 1);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_cond_branch(decoder, &taken);
	ptu_int_eq(errcode, -pte_bad_query);
	ptu_ptr_eq(decoder->pos, pos);
	ptu_int_eq(taken, tnt);

	return ptu_passed();
}

static struct ptunit_result
cond_skip_tip_pgd_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	int errcode, tnt = 0xbc, taken = tnt;
	const uint8_t *pos;

	pos = encoder->pos;
	pt_encode_tip_pgd(encoder, 0, pt_ipc_sext_48);
	pt_encode_tnt_8(encoder, 0, 1);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_cond_branch(decoder, &taken);
	ptu_int_eq(errcode, -pte_bad_query);
	ptu_ptr_eq(decoder->pos, pos);
	ptu_int_eq(taken, tnt);

	return ptu_passed();
}

static struct ptunit_result
cond_skip_fup_tip_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	int errcode, tnt = 0xbc, taken = tnt;
	const uint8_t *pos;

	pt_encode_fup(encoder, 0, pt_ipc_sext_48);
	pos = encoder->pos;
	pt_encode_tip(encoder, 0, pt_ipc_sext_48);
	pt_encode_tnt_8(encoder, 0, 1);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_cond_branch(decoder, &taken);
	ptu_int_eq(errcode, -pte_bad_query);
	ptu_ptr_eq(decoder->pos, pos);
	ptu_int_eq(taken, tnt);

	return ptu_passed();
}

static struct ptunit_result
cond_skip_fup_tip_pgd_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	int errcode, tnt = 0xbc, taken = tnt;
	const uint8_t *pos;

	pt_encode_fup(encoder, 0, pt_ipc_sext_48);
	pos = encoder->pos;
	pt_encode_tip_pgd(encoder, 0, pt_ipc_sext_48);
	pt_encode_tnt_8(encoder, 0, 1);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_cond_branch(decoder, &taken);
	ptu_int_eq(errcode, -pte_bad_query);
	ptu_ptr_eq(decoder->pos, pos);
	ptu_int_eq(taken, tnt);

	return ptu_passed();
}

static struct ptunit_result event_null(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_config *config = &decoder->config;
	struct pt_event event;
	int errcode;

	errcode = pt_qry_event(NULL, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_invalid);

	errcode = pt_qry_event(decoder, NULL, sizeof(event));
	ptu_int_eq(errcode, -pte_invalid);
	ptu_ptr_eq(decoder->pos, config->begin);

	return ptu_passed();
}

static struct ptunit_result event_bad_size(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_event event;
	int errcode;

	errcode = pt_qry_event(decoder, &event, 4);
	ptu_int_eq(errcode, -pte_invalid);

	return ptu_passed();
}

static struct ptunit_result event_small_size(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	union {
		struct pt_event event;
		uint8_t buffer[41];
	} variant;
	int errcode;

	memset(variant.buffer, 0xcd, sizeof(variant.buffer));

	pt_encode_tip_pge(encoder, 0ull, pt_ipc_sext_48);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &variant.event, 40);
	ptu_int_eq(errcode, pts_eos);
	ptu_int_eq(variant.event.type, ptev_enabled);
	ptu_uint_eq(variant.buffer[40], 0xcd);

	return ptu_passed();
}

static struct ptunit_result event_big_size(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	union {
		struct pt_event event;
		uint8_t buffer[1024];
	} variant;
	int errcode;

	memset(variant.buffer, 0xcd, sizeof(variant.buffer));

	pt_encode_tip_pge(encoder, 0ull, pt_ipc_sext_48);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &variant.event, sizeof(variant.buffer));
	ptu_int_eq(errcode, pts_eos);
	ptu_int_eq(variant.event.type, ptev_enabled);
	ptu_uint_eq(variant.buffer[sizeof(variant.event)], 0xcd);

	return ptu_passed();
}

static struct ptunit_result event_empty(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_config *config = &decoder->config;
	struct pt_event event;
	int errcode;

	decoder->pos = config->end;

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result event_enabled(struct ptu_decoder_fixture *dfix,
					  enum pt_ip_compression ipc,
					  uint64_t tsc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_packet_ip packet;
	struct pt_event event;
	int errcode;

	packet.ipc = ipc;
	packet.ip = pt_dfix_max_ip;
	pt_last_ip_update_ip(&dfix->last_ip, &packet, &dfix->config);

	pt_encode_tip_pge(encoder, packet.ip, packet.ipc);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	if (ipc == pt_ipc_suppressed)
		ptu_int_eq(errcode, -pte_bad_packet);
	else {
		ptu_int_eq(errcode, pts_eos);
		ptu_int_eq(event.type, ptev_enabled);
		ptu_uint_eq(event.variant.enabled.ip, dfix->last_ip.ip);

		if (!tsc)
			ptu_int_eq(event.has_tsc, 0);
		else {
			ptu_int_eq(event.has_tsc, 1);
			ptu_uint_eq(event.tsc, tsc);
		}
	}

	return ptu_passed();
}

static struct ptunit_result
event_enabled_cutoff_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_tip_pge(encoder, 0, pt_ipc_sext_48);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result event_disabled(struct ptu_decoder_fixture *dfix,
					   enum pt_ip_compression ipc,
					   uint64_t tsc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_packet_ip packet;
	struct pt_event event;
	int errcode;

	packet.ipc = ipc;
	packet.ip = pt_dfix_sext_ip;
	pt_last_ip_update_ip(&dfix->last_ip, &packet, &dfix->config);

	pt_encode_tip_pgd(encoder, packet.ip, packet.ipc);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, pts_eos);
	if (ipc == pt_ipc_suppressed)
		ptu_uint_ne(event.ip_suppressed, 0);
	else {
		ptu_uint_eq(event.ip_suppressed, 0);
		ptu_uint_eq(event.variant.disabled.ip, dfix->last_ip.ip);
	}
	ptu_int_eq(event.type, ptev_disabled);

	if (!tsc)
		ptu_int_eq(event.has_tsc, 0);
	else {
		ptu_int_eq(event.has_tsc, 1);
		ptu_uint_eq(event.tsc, tsc);
	}

	return ptu_passed();
}

static struct ptunit_result
event_disabled_cutoff_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_tip_pgd(encoder, 0, pt_ipc_update_32);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result
event_async_disabled(struct ptu_decoder_fixture *dfix,
		     enum pt_ip_compression ipc, uint64_t tsc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_packet_ip fup, tip;
	struct pt_event event;
	int errcode;

	fup.ipc = pt_ipc_sext_48;
	fup.ip = pt_dfix_max_ip;
	pt_last_ip_update_ip(&dfix->last_ip, &fup, &dfix->config);

	tip.ipc = ipc;
	tip.ip = pt_dfix_sext_ip;
	pt_last_ip_update_ip(&dfix->last_ip, &tip, &dfix->config);

	pt_encode_fup(encoder, fup.ip, fup.ipc);
	pt_encode_tip_pgd(encoder, tip.ip, tip.ipc);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, pts_eos);
	if (ipc == pt_ipc_suppressed)
		ptu_uint_ne(event.ip_suppressed, 0);
	else {
		ptu_uint_eq(event.ip_suppressed, 0);
		ptu_uint_eq(event.variant.async_disabled.ip, dfix->last_ip.ip);
	}
	ptu_int_eq(event.type, ptev_async_disabled);
	ptu_uint_eq(event.variant.async_disabled.at, fup.ip);

	if (!tsc)
		ptu_int_eq(event.has_tsc, 0);
	else {
		ptu_int_eq(event.has_tsc, 1);
		ptu_uint_eq(event.tsc, tsc);
	}

	return ptu_passed();
}

static struct ptunit_result
event_async_disabled_suppressed_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_fup(encoder, 0, pt_ipc_suppressed);
	pt_encode_tip_pgd(encoder, 0, pt_ipc_sext_48);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_ip_suppressed);

	return ptu_passed();
}

static struct ptunit_result
event_async_disabled_cutoff_fail_a(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	uint64_t at = pt_dfix_sext_ip;
	int errcode;

	pt_encode_fup(encoder, at, pt_ipc_sext_48);
	pt_encode_tip_pgd(encoder, 0, pt_ipc_update_16);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result
event_async_disabled_cutoff_fail_b(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_fup(encoder, 0, pt_ipc_sext_48);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result
event_async_branch_suppressed_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_fup(encoder, 0, pt_ipc_suppressed);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_ip_suppressed);

	return ptu_passed();
}

static struct ptunit_result event_async_branch(struct ptu_decoder_fixture *dfix,
					       enum pt_ip_compression ipc,
					       uint64_t tsc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_packet_ip fup, tip;
	struct pt_event event;
	int errcode;

	fup.ipc = pt_ipc_sext_48;
	fup.ip = pt_dfix_max_ip;
	pt_last_ip_update_ip(&dfix->last_ip, &fup, &dfix->config);

	tip.ipc = ipc;
	tip.ip = pt_dfix_sext_ip;
	pt_last_ip_update_ip(&dfix->last_ip, &tip, &dfix->config);

	pt_encode_fup(encoder, fup.ip, fup.ipc);
	pt_encode_tip(encoder, tip.ip, tip.ipc);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, pts_eos);
	if (ipc == pt_ipc_suppressed)
		ptu_uint_ne(event.ip_suppressed, 0);
	else {
		ptu_uint_eq(event.ip_suppressed, 0);
		ptu_uint_eq(event.variant.async_branch.to, dfix->last_ip.ip);
	}
	ptu_int_eq(event.type, ptev_async_branch);
	ptu_uint_eq(event.variant.async_branch.from, fup.ip);

	if (!tsc)
		ptu_int_eq(event.has_tsc, 0);
	else {
		ptu_int_eq(event.has_tsc, 1);
		ptu_uint_eq(event.tsc, tsc);
	}

	return ptu_passed();
}

static struct ptunit_result
event_async_branch_cutoff_fail_a(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_fup(encoder, 0, pt_ipc_sext_48);
	pt_encode_tip_pgd(encoder, 0, pt_ipc_update_16);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result
event_async_branch_cutoff_fail_b(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_fup(encoder, 0, pt_ipc_sext_48);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result event_paging(struct ptu_decoder_fixture *dfix,
					 uint8_t flags, uint64_t tsc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	uint64_t cr3 = pt_dfix_max_cr3;
	int errcode;

	pt_encode_pip(encoder, cr3, flags);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, pts_eos);
	ptu_int_eq(event.type, ptev_paging);
	ptu_uint_eq(event.variant.paging.cr3, cr3);
	ptu_uint_eq(event.variant.paging.non_root, (flags & pt_pl_pip_nr) != 0);

	if (!tsc)
		ptu_int_eq(event.has_tsc, 0);
	else {
		ptu_int_eq(event.has_tsc, 1);
		ptu_uint_eq(event.tsc, tsc);
	}

	return ptu_passed();
}

static struct ptunit_result
event_paging_cutoff_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_pip(encoder, 0, 0);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result
event_async_paging(struct ptu_decoder_fixture *dfix, uint8_t flags,
		   uint64_t tsc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	uint64_t to = pt_dfix_sext_ip, from = to & ~0xffffull;
	uint64_t cr3 = pt_dfix_max_cr3;
	int errcode;

	pt_encode_fup(encoder, from, pt_ipc_sext_48);
	pt_encode_pip(encoder, cr3, flags);
	pt_encode_tip(encoder, to, pt_ipc_update_16);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, pts_event_pending);
	ptu_int_eq(event.type, ptev_async_branch);
	ptu_uint_eq(event.variant.async_branch.from, from);
	ptu_uint_eq(event.variant.async_branch.to, to);

	if (!tsc)
		ptu_int_eq(event.has_tsc, 0);
	else {
		ptu_int_eq(event.has_tsc, 1);
		ptu_uint_eq(event.tsc, tsc);
	}

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, pts_eos);
	ptu_int_eq(event.type, ptev_async_paging);
	ptu_uint_eq(event.variant.async_paging.cr3, cr3);
	ptu_uint_eq(event.variant.async_paging.non_root,
		    (flags & pt_pl_pip_nr) != 0);
	ptu_uint_eq(event.variant.async_paging.ip, to);

	if (!tsc)
		ptu_int_eq(event.has_tsc, 0);
	else {
		ptu_int_eq(event.has_tsc, 1);
		ptu_uint_eq(event.tsc, tsc);
	}

	return ptu_passed();
}

static struct ptunit_result
event_async_paging_suppressed(struct ptu_decoder_fixture *dfix, uint8_t flags,
			      uint64_t tsc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	uint64_t from = pt_dfix_sext_ip, cr3 = pt_dfix_max_cr3;
	int errcode;

	pt_encode_fup(encoder, from, pt_ipc_sext_48);
	pt_encode_pip(encoder, cr3, flags);
	pt_encode_tip(encoder, 0, pt_ipc_suppressed);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, pts_event_pending);
	ptu_uint_ne(event.ip_suppressed, 0);
	ptu_int_eq(event.type, ptev_async_branch);
	ptu_uint_eq(event.variant.async_branch.from, from);

	if (!tsc)
		ptu_int_eq(event.has_tsc, 0);
	else {
		ptu_int_eq(event.has_tsc, 1);
		ptu_uint_eq(event.tsc, tsc);
	}

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, pts_eos);
	ptu_uint_ne(event.ip_suppressed, 0);
	ptu_int_eq(event.type, ptev_async_paging);
	ptu_uint_eq(event.variant.async_paging.cr3, cr3);
	ptu_uint_eq(event.variant.async_paging.non_root,
		    (flags & pt_pl_pip_nr) != 0);

	if (!tsc)
		ptu_int_eq(event.has_tsc, 0);
	else {
		ptu_int_eq(event.has_tsc, 1);
		ptu_uint_eq(event.tsc, tsc);
	}

	return ptu_passed();
}

static struct ptunit_result
event_async_paging_cutoff_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_fup(encoder, 0, pt_ipc_sext_48);
	pt_encode_pip(encoder, 0, 0);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result event_overflow_fup(struct ptu_decoder_fixture *dfix,
					       enum pt_ip_compression ipc,
					       uint64_t tsc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	struct pt_packet_ip packet;
	int errcode;

	packet.ipc = ipc;
	packet.ip = 0xccull;

	pt_last_ip_init(&dfix->last_ip);
	pt_last_ip_update_ip(&dfix->last_ip, &packet, &dfix->config);

	pt_encode_ovf(encoder);
	pt_encode_fup(encoder, packet.ip, packet.ipc);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	switch (ipc) {
	case pt_ipc_suppressed:
		ptu_int_eq(errcode, -pte_noip);
		break;

	case pt_ipc_update_16:
	case pt_ipc_update_32:
	case pt_ipc_update_48:
	case pt_ipc_sext_48:
	case pt_ipc_full:
		ptu_int_eq(errcode, pts_eos);
		ptu_int_eq(event.type, ptev_overflow);
		ptu_uint_eq(event.variant.overflow.ip, dfix->last_ip.ip);

		if (!tsc)
			ptu_int_eq(event.has_tsc, 0);
		else {
			ptu_int_eq(event.has_tsc, 1);
			ptu_uint_eq(event.tsc, tsc);
		}
		break;
	}

	return ptu_passed();
}

static struct ptunit_result
event_overflow_tip_pge(struct ptu_decoder_fixture *dfix,
		       enum pt_ip_compression ipc, uint64_t tsc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	struct pt_packet_ip packet;
	int errcode;

	packet.ipc = ipc;
	packet.ip = 0xccull;

	pt_last_ip_init(&dfix->last_ip);
	pt_last_ip_update_ip(&dfix->last_ip, &packet, &dfix->config);

	pt_encode_ovf(encoder);
	pt_encode_tip_pge(encoder, packet.ip, packet.ipc);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, pts_event_pending);
	ptu_int_eq(event.type, ptev_overflow);
	ptu_uint_ne(event.ip_suppressed, 0);

	if (!tsc)
		ptu_int_eq(event.has_tsc, 0);
	else {
		ptu_int_eq(event.has_tsc, 1);
		ptu_uint_eq(event.tsc, tsc);
	}

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	switch (ipc) {
	case pt_ipc_suppressed:
		ptu_int_eq(errcode, -pte_bad_packet);
		break;

	case pt_ipc_update_16:
	case pt_ipc_update_32:
	case pt_ipc_update_48:
	case pt_ipc_sext_48:
	case pt_ipc_full:
		ptu_int_eq(errcode, pts_eos);
		ptu_int_eq(event.type, ptev_enabled);
		ptu_uint_eq(event.variant.enabled.ip, dfix->last_ip.ip);

		if (!tsc)
			ptu_int_eq(event.has_tsc, 0);
		else {
			ptu_int_eq(event.has_tsc, 1);
			ptu_uint_eq(event.tsc, tsc);
		}
		break;
	}

	return ptu_passed();
}

static struct ptunit_result
event_overflow_cutoff_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_ovf(encoder);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result event_stop(struct ptu_decoder_fixture *dfix,
				       uint64_t tsc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_stop(encoder);

	ptu_sync_decoder(decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, pts_eos);
	ptu_int_eq(event.type, ptev_stop);

	if (!tsc)
		ptu_int_eq(event.has_tsc, 0);
	else {
		ptu_int_eq(event.has_tsc, 1);
		ptu_uint_eq(event.tsc, tsc);
	}

	return ptu_passed();
}

static struct ptunit_result
event_exec_mode_tip(struct ptu_decoder_fixture *dfix,
		    enum pt_ip_compression ipc, uint64_t tsc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	enum pt_exec_mode mode = ptem_16bit;
	struct pt_packet_ip packet;
	struct pt_event event;
	uint64_t addr = 0ull;
	int errcode;

	packet.ipc = ipc;
	packet.ip = pt_dfix_max_ip;
	pt_last_ip_update_ip(&dfix->last_ip, &packet, &dfix->config);

	pt_encode_mode_exec(encoder, mode);
	pt_encode_tip(encoder, packet.ip, packet.ipc);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, 0);
	if (ipc == pt_ipc_suppressed)
		ptu_uint_ne(event.ip_suppressed, 0);
	else {
		ptu_uint_eq(event.ip_suppressed, 0);
		ptu_uint_eq(event.variant.exec_mode.ip, dfix->last_ip.ip);
	}
	ptu_int_eq(event.type, ptev_exec_mode);
	ptu_int_eq(event.variant.exec_mode.mode, mode);

	if (!tsc)
		ptu_int_eq(event.has_tsc, 0);
	else {
		ptu_int_eq(event.has_tsc, 1);
		ptu_uint_eq(event.tsc, tsc);
	}

	errcode = pt_qry_indirect_branch(decoder, &addr);
	if (ipc == pt_ipc_suppressed)
		ptu_int_eq(errcode, pts_ip_suppressed | pts_eos);
	else {
		ptu_int_eq(errcode, pts_eos);
		ptu_uint_eq(addr, dfix->last_ip.ip);
	}

	return ptu_passed();
}

static struct ptunit_result
event_exec_mode_tip_cutoff_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_mode_exec(encoder, ptem_32bit);
	pt_encode_tip(encoder, 0, pt_ipc_update_16);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result
event_exec_mode_tip_pge(struct ptu_decoder_fixture *dfix,
			enum pt_ip_compression ipc, uint64_t tsc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	enum pt_exec_mode mode = ptem_16bit;
	struct pt_packet_ip packet;
	struct pt_event event;
	uint64_t addr = 0ull;
	int errcode;

	packet.ipc = ipc;
	packet.ip = pt_dfix_max_ip;
	pt_last_ip_update_ip(&dfix->last_ip, &packet, &dfix->config);

	pt_encode_mode_exec(encoder, mode);
	pt_encode_tip_pge(encoder, packet.ip, packet.ipc);

	ptu_check(ptu_sync_decoder, decoder);
	decoder->enabled = 0;

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	if (ipc == pt_ipc_suppressed) {
		ptu_int_eq(errcode, -pte_bad_packet);
		ptu_uint_eq(addr, 0ull);
	} else {
		ptu_int_eq(errcode, pts_event_pending);
		ptu_int_eq(event.type, ptev_enabled);
		ptu_uint_eq(event.variant.enabled.ip, dfix->last_ip.ip);

		if (!tsc)
			ptu_int_eq(event.has_tsc, 0);
		else {
			ptu_int_eq(event.has_tsc, 1);
			ptu_uint_eq(event.tsc, tsc);
		}

		errcode = pt_qry_event(decoder, &event, sizeof(event));
		ptu_int_eq(errcode, pts_eos);
		ptu_int_eq(event.type, ptev_exec_mode);
		ptu_int_eq(event.variant.exec_mode.mode, mode);
		ptu_uint_eq(event.variant.exec_mode.ip, dfix->last_ip.ip);

		if (!tsc)
			ptu_int_eq(event.has_tsc, 0);
		else {
			ptu_int_eq(event.has_tsc, 1);
			ptu_uint_eq(event.tsc, tsc);
		}
	}

	return ptu_passed();
}

static struct ptunit_result
event_exec_mode_tip_pge_cutoff_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_mode_exec(encoder, ptem_16bit);
	pt_encode_tip_pge(encoder, 0, pt_ipc_sext_48);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result
event_exec_mode_cutoff_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_mode_exec(encoder, ptem_64bit);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result event_tsx_fup(struct ptu_decoder_fixture *dfix,
					  enum pt_ip_compression ipc,
					  uint8_t flags, uint64_t tsc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_packet_ip fup, tip;
	struct pt_event event;
	uint64_t addr = 0;
	int errcode;

	fup.ipc = ipc;
	fup.ip = pt_dfix_max_ip;
	pt_last_ip_update_ip(&dfix->last_ip, &fup, &dfix->config);

	tip.ipc = pt_ipc_sext_48;
	tip.ip = pt_dfix_sext_ip;

	pt_encode_mode_tsx(encoder, flags);
	pt_encode_fup(encoder, fup.ip, fup.ipc);
	pt_encode_tip(encoder, tip.ip, tip.ipc);

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, 0);
	if (ipc == pt_ipc_suppressed)
		ptu_uint_ne(event.ip_suppressed, 0);
	else {
		ptu_uint_eq(event.ip_suppressed, 0);
		ptu_uint_eq(event.variant.tsx.ip, dfix->last_ip.ip);
	}
	ptu_int_eq(event.type, ptev_tsx);
	ptu_int_eq(event.variant.tsx.speculative,
		   (flags & pt_mob_tsx_intx) != 0);
	ptu_int_eq(event.variant.tsx.aborted,
		   (flags & pt_mob_tsx_abrt) != 0);

	if (!tsc)
		ptu_int_eq(event.has_tsc, 0);
	else {
		ptu_int_eq(event.has_tsc, 1);
		ptu_uint_eq(event.tsc, tsc);
	}

	errcode = pt_qry_indirect_branch(decoder, &addr);
	ptu_int_eq(errcode, pts_eos);
	ptu_uint_eq(addr, tip.ip);

	return ptu_passed();
}

static struct ptunit_result
event_tsx_fup_cutoff_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_mode_tsx(encoder, 0);
	pt_encode_fup(encoder, 0, pt_ipc_update_16);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result
event_tsx_cutoff_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_mode_tsx(encoder, 0);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result
event_skip_tip_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	const uint8_t *pos;
	int errcode;

	pos = encoder->pos;
	pt_encode_tip(encoder, 0, pt_ipc_sext_48);
	/* We omit the actual event - we don't get that far, anyway. */

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_bad_query);
	ptu_ptr_eq(decoder->pos, pos);

	return ptu_passed();
}

static struct ptunit_result
event_skip_tnt_8_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_tnt_8(encoder, 0, 1);
	pt_encode_tnt_8(encoder, 0, 1);
	/* We omit the actual event - we don't get that far, anyway. */

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_bad_query);
	/* The fail position depends on the fixture's header. */

	return ptu_passed();
}

static struct ptunit_result
event_skip_tnt_64_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_tnt_64(encoder, 0, 1);
	pt_encode_tnt_64(encoder, 0, 1);
	/* We omit the actual event - we don't get that far, anyway. */

	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, -pte_bad_query);
	/* The fail position depends on the fixture's header. */

	return ptu_passed();
}

static struct ptunit_result sync_event(struct ptu_decoder_fixture *dfix,
				       enum pt_ip_compression ipc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_packet_ip packet;
	struct pt_event event;
	uint64_t addr = 0ull;
	int errcode;

	packet.ipc = ipc;
	packet.ip = 0xccull;

	pt_last_ip_init(&dfix->last_ip);
	pt_last_ip_update_ip(&dfix->last_ip, &packet, &dfix->config);

	pt_encode_psb(encoder);
	pt_encode_mode_tsx(encoder, pt_mob_tsx_intx);
	pt_encode_fup(encoder, packet.ip, packet.ipc);
	pt_encode_psbend(encoder);

	errcode = pt_qry_sync_forward(decoder, &addr);
	switch (ipc) {
	case pt_ipc_suppressed:
		ptu_int_eq(errcode, (pts_event_pending | pts_ip_suppressed));
		break;

	case pt_ipc_update_16:
	case pt_ipc_update_32:
	case pt_ipc_update_48:
	case pt_ipc_sext_48:
	case pt_ipc_full:
		ptu_int_eq(errcode, pts_event_pending);
		ptu_uint_eq(addr, dfix->last_ip.ip);
		break;
	}

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, pts_eos);
	ptu_uint_ne(event.status_update, 0);
	if (ipc == pt_ipc_suppressed)
		ptu_uint_ne(event.ip_suppressed, 0);
	else {
		ptu_uint_eq(event.ip_suppressed, 0);
		ptu_uint_eq(event.variant.tsx.ip, dfix->last_ip.ip);
	}
	ptu_int_eq(event.type, ptev_tsx);
	ptu_int_eq(event.variant.tsx.speculative, 1);
	ptu_int_eq(event.variant.tsx.aborted, 0);
	ptu_int_eq(event.has_tsc, 0);

	return ptu_passed();
}

static struct ptunit_result
sync_event_cutoff_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	uint64_t addr;
	int errcode;

	pt_encode_psb(encoder);
	pt_encode_psbend(encoder);

	ptu_check(cutoff, decoder, encoder);

	errcode = pt_qry_sync_forward(decoder, &addr);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result
sync_event_incomplete_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	uint64_t addr;
	int errcode;

	pt_encode_psb(encoder);

	errcode = pt_qry_sync_forward(decoder, &addr);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result sync_ovf_event(struct ptu_decoder_fixture *dfix,
					   enum pt_ip_compression ipc)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_packet_ip fup, ovf;
	struct pt_event event;
	uint64_t addr = 0;
	int errcode;

	fup.ipc = pt_ipc_sext_48;
	fup.ip = pt_dfix_max_ip;

	ovf.ipc = ipc;
	ovf.ip = 0xccull;

	pt_last_ip_init(&dfix->last_ip);
	pt_last_ip_update_ip(&dfix->last_ip, &ovf, &dfix->config);

	pt_encode_psb(encoder);
	pt_encode_fup(encoder, fup.ip, fup.ipc);
	pt_encode_mode_tsx(encoder, 0);
	pt_encode_tsc(encoder, 0x1000);
	pt_encode_ovf(encoder);
	pt_encode_fup(encoder, ovf.ip, ovf.ipc);

	errcode = pt_qry_sync_forward(decoder, &addr);
	ptu_int_eq(errcode, pts_event_pending);
	ptu_uint_eq(addr, fup.ip);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, pts_event_pending);
	ptu_uint_ne(event.status_update, 0);
	ptu_int_eq(event.type, ptev_tsx);
	ptu_int_eq(event.variant.tsx.speculative, 0);
	ptu_int_eq(event.variant.tsx.aborted, 0);
	ptu_uint_eq(event.variant.tsx.ip, fup.ip);
	ptu_int_eq(event.has_tsc, 1);
	ptu_uint_eq(event.tsc, 0x1000);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	switch (ipc) {
	case pt_ipc_suppressed:
		ptu_int_eq(errcode, -pte_noip);
		return ptu_passed();

	case pt_ipc_update_16:
	case pt_ipc_update_32:
	case pt_ipc_update_48:
	case pt_ipc_sext_48:
	case pt_ipc_full:
		ptu_int_eq(errcode, pts_eos);
		ptu_int_eq(event.type, ptev_overflow);
		ptu_uint_eq(event.variant.overflow.ip, dfix->last_ip.ip);
		ptu_int_eq(event.has_tsc, 1);
		ptu_uint_eq(event.tsc, 0x1000);
		break;
	}

	return ptu_passed();
}

static struct ptunit_result
sync_ovf_event_cutoff_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	uint64_t addr;
	int errcode;

	pt_encode_psb(encoder);
	pt_encode_ovf(encoder);

	ptu_check(cutoff, decoder, encoder);

	errcode = pt_qry_sync_forward(decoder, &addr);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result time_null_fail(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	uint64_t tsc;
	int errcode;

	errcode = pt_qry_time(NULL, NULL, NULL, NULL);
	ptu_int_eq(errcode, -pte_invalid);

	errcode = pt_qry_time(decoder, NULL, NULL, NULL);
	ptu_int_eq(errcode, -pte_invalid);

	errcode = pt_qry_time(NULL, &tsc, NULL, NULL);
	ptu_int_eq(errcode, -pte_invalid);

	return ptu_passed();
}

static struct ptunit_result time_initial(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	uint64_t tsc;
	int errcode;

	errcode = pt_qry_time(decoder, &tsc, NULL, NULL);
	ptu_int_eq(errcode, -pte_no_time);

	return ptu_passed();
}

static struct ptunit_result time(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	uint64_t tsc, exp;
	int errcode;

	exp = 0x11223344556677ull;

	decoder->last_time.have_tsc = 1;
	decoder->last_time.tsc = exp;

	errcode = pt_qry_time(decoder, &tsc, NULL, NULL);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(tsc, exp);

	return ptu_passed();
}

static struct ptunit_result cbr_null(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	uint32_t cbr;
	int errcode;

	errcode = pt_qry_core_bus_ratio(NULL, NULL);
	ptu_int_eq(errcode, -pte_invalid);

	errcode = pt_qry_core_bus_ratio(decoder, NULL);
	ptu_int_eq(errcode, -pte_invalid);

	errcode = pt_qry_core_bus_ratio(NULL, &cbr);
	ptu_int_eq(errcode, -pte_invalid);

	return ptu_passed();
}

static struct ptunit_result cbr_initial(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	uint32_t cbr;
	int errcode;

	errcode = pt_qry_core_bus_ratio(decoder, &cbr);
	ptu_int_eq(errcode, -pte_no_cbr);

	return ptu_passed();
}

static struct ptunit_result cbr(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	uint32_t cbr;
	int errcode;

	decoder->last_time.have_cbr = 1;
	decoder->last_time.cbr = 42;

	errcode = pt_qry_core_bus_ratio(decoder, &cbr);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(cbr, 42);

	return ptu_passed();
}

/* Test that end-of-stream is indicated correctly when the stream ends with a
 * partial non-query-relevant packet.
 */
static struct ptunit_result indir_cyc_cutoff(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	uint64_t ip;
	int errcode;

	pt_encode_tip(encoder, 0xa000ull, pt_ipc_full);
	pt_encode_cyc(encoder, 0xfff);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_indirect_branch(decoder, &ip);
	ptu_int_eq(errcode, pts_eos);

	return ptu_passed();
}

/* Test that end-of-stream is indicated correctly when the stream ends with a
 * partial non-query-relevant packet.
 */
static struct ptunit_result cond_cyc_cutoff(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	int errcode, taken;

	pt_encode_tnt_8(encoder, 0, 1);
	pt_encode_cyc(encoder, 0xfff);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_cond_branch(decoder, &taken);
	ptu_int_eq(errcode, pts_eos);

	return ptu_passed();
}

/* Test that end-of-stream is indicated correctly when the stream ends with a
 * partial non-query-relevant packet.
 */
static struct ptunit_result event_cyc_cutoff(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;
	struct pt_event event;
	int errcode;

	pt_encode_tip_pgd(encoder, 0ull, pt_ipc_full);
	pt_encode_cyc(encoder, 0xffff);

	ptu_check(cutoff, decoder, encoder);
	ptu_check(ptu_sync_decoder, decoder);

	errcode = pt_qry_event(decoder, &event, sizeof(event));
	ptu_int_eq(errcode, pts_eos);

	return ptu_passed();
}

static struct ptunit_result ptu_dfix_init(struct ptu_decoder_fixture *dfix)
{
	struct pt_config *config = &dfix->config;
	int errcode;

	(void) memset(dfix->buffer, 0, sizeof(dfix->buffer));

	pt_config_init(config);

	config->begin = dfix->buffer;
	config->end = dfix->buffer + sizeof(dfix->buffer);

	errcode = pt_encoder_init(&dfix->encoder, config);
	ptu_int_eq(errcode, 0);

	errcode = pt_qry_decoder_init(&dfix->decoder, config);
	ptu_int_eq(errcode, 0);

	dfix->decoder.ip.ip = pt_dfix_bad_ip;
	dfix->decoder.ip.have_ip = 1;
	dfix->decoder.ip.suppressed = 0;

	dfix->last_ip = dfix->decoder.ip;

	if (dfix->header)
		dfix->header(dfix);

	return ptu_passed();
}

static struct ptunit_result ptu_dfix_fini(struct ptu_decoder_fixture *dfix)
{
	pt_qry_decoder_fini(&dfix->decoder);
	pt_encoder_fini(&dfix->encoder);

	return ptu_passed();
}

/* Synchronize the decoder at the beginnig of an empty buffer. */
static struct ptunit_result
ptu_dfix_header_sync(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;

	/* Synchronize the decoder at the beginning of the buffer. */
	decoder->pos = decoder->config.begin;

	return ptu_passed();
}

/* Synchronize the decoder at the beginnig of a buffer containing packets that
 * should be skipped for unconditional indirect branch queries.
 */
static struct ptunit_result
ptu_dfix_header_indir(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;

	pt_encode_pad(encoder);
	pt_encode_mtc(encoder, 1);
	pt_encode_pad(encoder);
	pt_encode_tsc(encoder, 0);

	/* Synchronize the decoder at the beginning of the buffer. */
	decoder->pos = decoder->config.begin;

	return ptu_passed();
}

/* Synchronize the decoder at the beginnig of a buffer containing packets that
 * should be skipped for unconditional indirect branch queries including a PSB.
 */
static struct ptunit_result
ptu_dfix_header_indir_psb(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;

	/* The psb must be empty since the tests won't skip status events.
	 * On the other hand, we do need to provide an address since tests
	 * may want to update last-ip, which requires a last-ip, of course.
	 */
	pt_encode_pad(encoder);
	pt_encode_tsc(encoder, 0);
	pt_encode_psb(encoder);
	pt_encode_mtc(encoder, 1);
	pt_encode_pad(encoder);
	pt_encode_tsc(encoder, 0);
	pt_encode_fup(encoder, pt_dfix_sext_ip, pt_ipc_sext_48);
	pt_encode_psbend(encoder);
	pt_encode_mtc(encoder, 1);
	pt_encode_pad(encoder);

	/* Synchronize the decoder at the beginning of the buffer. */
	decoder->pos = decoder->config.begin;

	return ptu_passed();
}

/* Synchronize the decoder at the beginnig of a buffer containing packets that
 * should be skipped for conditional branch queries.
 */
static struct ptunit_result
ptu_dfix_header_cond(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;

	/* The psb must be empty since the tests won't skip status events.
	 * On the other hand, we do need to provide an address since tests
	 * may want to update last-ip, which requires a last-ip, of course.
	 */
	pt_encode_pad(encoder);
	pt_encode_mtc(encoder, 1);
	pt_encode_psb(encoder);
	pt_encode_tsc(encoder, 0);
	pt_encode_pad(encoder);
	pt_encode_fup(encoder, pt_dfix_sext_ip, pt_ipc_sext_48);
	pt_encode_psbend(encoder);
	pt_encode_pad(encoder);
	pt_encode_tsc(encoder, 0);
	pt_encode_pad(encoder);

	/* Synchronize the decoder at the beginning of the buffer. */
	decoder->pos = decoder->config.begin;

	return ptu_passed();
}

/* Synchronize the decoder at the beginnig of a buffer containing packets that
 * should be skipped for event queries.
 */
static struct ptunit_result
ptu_dfix_header_event(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;

	pt_encode_pad(encoder);
	pt_encode_mtc(encoder, 1);
	pt_encode_pad(encoder);
	pt_encode_tsc(encoder, 0x1000);

	/* Synchronize the decoder at the beginning of the buffer. */
	decoder->pos = decoder->config.begin;

	return ptu_passed();
}

/* Synchronize the decoder at the beginnig of a buffer containing packets that
 * should be skipped for event queries including a PSB.
 */
static struct ptunit_result
ptu_dfix_header_event_psb(struct ptu_decoder_fixture *dfix)
{
	struct pt_query_decoder *decoder = &dfix->decoder;
	struct pt_encoder *encoder = &dfix->encoder;

	/* The psb must be empty since the tests won't skip status events.
	 * On the other hand, we do need to provide an address since tests
	 * may want to update last-ip, which requires a last-ip, of course.
	 */
	pt_encode_pad(encoder);
	pt_encode_tsc(encoder, 0);
	pt_encode_psb(encoder);
	pt_encode_mtc(encoder, 1);
	pt_encode_pad(encoder);
	pt_encode_tsc(encoder, 0x1000);
	pt_encode_fup(encoder, pt_dfix_sext_ip, pt_ipc_sext_48);
	pt_encode_psbend(encoder);
	pt_encode_mtc(encoder, 1);
	pt_encode_pad(encoder);

	/* Synchronize the decoder at the beginning of the buffer. */
	decoder->pos = decoder->config.begin;

	return ptu_passed();
}

static struct ptu_decoder_fixture dfix_raw;
static struct ptu_decoder_fixture dfix_empty;
static struct ptu_decoder_fixture dfix_indir;
static struct ptu_decoder_fixture dfix_indir_psb;
static struct ptu_decoder_fixture dfix_cond;
static struct ptu_decoder_fixture dfix_event;
static struct ptu_decoder_fixture dfix_event_psb;

static void init_fixtures(void)
{
	dfix_raw.init = ptu_dfix_init;
	dfix_raw.fini = ptu_dfix_fini;

	dfix_empty = dfix_raw;
	dfix_empty.header = ptu_dfix_header_sync;

	dfix_indir = dfix_raw;
	dfix_indir.header = ptu_dfix_header_indir;

	dfix_indir_psb = dfix_raw;
	dfix_indir_psb.header = ptu_dfix_header_indir_psb;

	dfix_cond = dfix_raw;
	dfix_cond.header = ptu_dfix_header_cond;

	dfix_event = dfix_raw;
	dfix_event.header = ptu_dfix_header_event;

	dfix_event_psb = dfix_raw;
	dfix_event_psb.header = ptu_dfix_header_event_psb;
}

int main(int argc, char **argv)
{
	struct ptunit_suite suite;

	init_fixtures();

	suite = ptunit_mk_suite(argc, argv);

	ptu_run_f(suite, indir_not_synced, dfix_raw);
	ptu_run_f(suite, cond_not_synced, dfix_raw);
	ptu_run_f(suite, event_not_synced, dfix_raw);

	ptu_run_f(suite, sync_backward, dfix_raw);
	ptu_run_f(suite, sync_backward_empty_end, dfix_raw);
	ptu_run_f(suite, sync_backward_empty_mid, dfix_raw);
	ptu_run_f(suite, sync_backward_empty_begin, dfix_raw);
	ptu_run_f(suite, decode_sync_backward, dfix_raw);

	ptu_run_f(suite, indir_null, dfix_empty);
	ptu_run_f(suite, indir_empty, dfix_empty);
	ptu_run_fp(suite, indir, dfix_empty, pt_ipc_suppressed);
	ptu_run_fp(suite, indir, dfix_empty, pt_ipc_update_16);
	ptu_run_fp(suite, indir, dfix_empty, pt_ipc_update_32);
	ptu_run_fp(suite, indir, dfix_empty, pt_ipc_update_48);
	ptu_run_fp(suite, indir, dfix_empty, pt_ipc_sext_48);
	ptu_run_fp(suite, indir, dfix_empty, pt_ipc_full);
	ptu_run_fp(suite, indir_tnt, dfix_empty, pt_ipc_suppressed);
	ptu_run_fp(suite, indir_tnt, dfix_empty, pt_ipc_update_16);
	ptu_run_fp(suite, indir_tnt, dfix_empty, pt_ipc_update_32);
	ptu_run_fp(suite, indir_tnt, dfix_empty, pt_ipc_update_48);
	ptu_run_fp(suite, indir_tnt, dfix_empty, pt_ipc_sext_48);
	ptu_run_fp(suite, indir_tnt, dfix_empty, pt_ipc_full);
	ptu_run_f(suite, indir_cutoff_fail, dfix_empty);
	ptu_run_f(suite, indir_skip_tnt_fail, dfix_empty);
	ptu_run_f(suite, indir_skip_tip_pge_fail, dfix_empty);
	ptu_run_f(suite, indir_skip_tip_pgd_fail, dfix_empty);
	ptu_run_f(suite, indir_skip_fup_tip_fail, dfix_empty);
	ptu_run_f(suite, indir_skip_fup_tip_pgd_fail, dfix_empty);

	ptu_run_fp(suite, indir, dfix_indir, pt_ipc_suppressed);
	ptu_run_fp(suite, indir, dfix_indir, pt_ipc_update_16);
	ptu_run_fp(suite, indir, dfix_indir, pt_ipc_update_32);
	ptu_run_fp(suite, indir, dfix_indir, pt_ipc_update_48);
	ptu_run_fp(suite, indir, dfix_indir, pt_ipc_sext_48);
	ptu_run_fp(suite, indir, dfix_indir, pt_ipc_full);
	ptu_run_fp(suite, indir_tnt, dfix_indir, pt_ipc_suppressed);
	ptu_run_fp(suite, indir_tnt, dfix_indir, pt_ipc_update_16);
	ptu_run_fp(suite, indir_tnt, dfix_indir, pt_ipc_update_32);
	ptu_run_fp(suite, indir_tnt, dfix_indir, pt_ipc_update_48);
	ptu_run_fp(suite, indir_tnt, dfix_indir, pt_ipc_sext_48);
	ptu_run_fp(suite, indir_tnt, dfix_indir, pt_ipc_full);
	ptu_run_f(suite, indir_cutoff_fail, dfix_indir);
	ptu_run_f(suite, indir_skip_tnt_fail, dfix_indir);
	ptu_run_f(suite, indir_skip_tip_pge_fail, dfix_indir);
	ptu_run_f(suite, indir_skip_tip_pgd_fail, dfix_indir);
	ptu_run_f(suite, indir_skip_fup_tip_fail, dfix_indir);
	ptu_run_f(suite, indir_skip_fup_tip_pgd_fail, dfix_indir);

	ptu_run_fp(suite, indir, dfix_indir_psb, pt_ipc_suppressed);
	ptu_run_fp(suite, indir, dfix_indir_psb, pt_ipc_sext_48);
	ptu_run_fp(suite, indir, dfix_indir_psb, pt_ipc_full);
	ptu_run_fp(suite, indir_tnt, dfix_indir_psb, pt_ipc_suppressed);
	ptu_run_fp(suite, indir_tnt, dfix_indir_psb, pt_ipc_sext_48);
	ptu_run_fp(suite, indir_tnt, dfix_indir_psb, pt_ipc_full);
	ptu_run_f(suite, indir_cutoff_fail, dfix_indir_psb);
	ptu_run_f(suite, indir_skip_tnt_fail, dfix_indir_psb);
	ptu_run_f(suite, indir_skip_tip_pge_fail, dfix_indir_psb);
	ptu_run_f(suite, indir_skip_tip_pgd_fail, dfix_indir_psb);
	ptu_run_f(suite, indir_skip_fup_tip_fail, dfix_indir_psb);
	ptu_run_f(suite, indir_skip_fup_tip_pgd_fail, dfix_indir_psb);

	ptu_run_f(suite, cond_null, dfix_empty);
	ptu_run_f(suite, cond_empty, dfix_empty);
	ptu_run_f(suite, cond, dfix_empty);
	ptu_run_f(suite, cond_skip_tip_fail, dfix_empty);
	ptu_run_f(suite, cond_skip_tip_pge_fail, dfix_empty);
	ptu_run_f(suite, cond_skip_tip_pgd_fail, dfix_empty);
	ptu_run_f(suite, cond_skip_fup_tip_fail, dfix_empty);
	ptu_run_f(suite, cond_skip_fup_tip_pgd_fail, dfix_empty);

	ptu_run_f(suite, cond, dfix_cond);
	ptu_run_f(suite, cond_skip_tip_fail, dfix_cond);
	ptu_run_f(suite, cond_skip_tip_pge_fail, dfix_cond);
	ptu_run_f(suite, cond_skip_tip_pgd_fail, dfix_cond);
	ptu_run_f(suite, cond_skip_fup_tip_fail, dfix_cond);
	ptu_run_f(suite, cond_skip_fup_tip_pgd_fail, dfix_cond);

	ptu_run_f(suite, event_null, dfix_empty);
	ptu_run_f(suite, event_bad_size, dfix_empty);
	ptu_run_f(suite, event_small_size, dfix_empty);
	ptu_run_f(suite, event_big_size, dfix_empty);
	ptu_run_f(suite, event_empty, dfix_empty);
	ptu_run_fp(suite, event_enabled, dfix_empty, pt_ipc_suppressed, 0);
	ptu_run_fp(suite, event_enabled, dfix_empty, pt_ipc_update_16, 0);
	ptu_run_fp(suite, event_enabled, dfix_empty, pt_ipc_update_32, 0);
	ptu_run_fp(suite, event_enabled, dfix_empty, pt_ipc_update_48, 0);
	ptu_run_fp(suite, event_enabled, dfix_empty, pt_ipc_sext_48, 0);
	ptu_run_fp(suite, event_enabled, dfix_empty, pt_ipc_full, 0);
	ptu_run_f(suite, event_enabled_cutoff_fail, dfix_empty);
	ptu_run_fp(suite, event_disabled, dfix_empty, pt_ipc_suppressed, 0);
	ptu_run_fp(suite, event_disabled, dfix_empty, pt_ipc_update_16, 0);
	ptu_run_fp(suite, event_disabled, dfix_empty, pt_ipc_update_32, 0);
	ptu_run_fp(suite, event_disabled, dfix_empty, pt_ipc_update_48, 0);
	ptu_run_fp(suite, event_disabled, dfix_empty, pt_ipc_sext_48, 0);
	ptu_run_fp(suite, event_disabled, dfix_empty, pt_ipc_full, 0);
	ptu_run_f(suite, event_disabled_cutoff_fail, dfix_empty);
	ptu_run_fp(suite, event_async_disabled, dfix_empty, pt_ipc_suppressed,
		   0);
	ptu_run_fp(suite, event_async_disabled, dfix_empty, pt_ipc_update_16,
		   0);
	ptu_run_fp(suite, event_async_disabled, dfix_empty, pt_ipc_update_32,
		   0);
	ptu_run_fp(suite, event_async_disabled, dfix_empty, pt_ipc_update_48,
		   0);
	ptu_run_fp(suite, event_async_disabled, dfix_empty, pt_ipc_sext_48, 0);
	ptu_run_fp(suite, event_async_disabled, dfix_empty, pt_ipc_full, 0);
	ptu_run_f(suite, event_async_disabled_suppressed_fail, dfix_empty);
	ptu_run_f(suite, event_async_disabled_cutoff_fail_a, dfix_empty);
	ptu_run_f(suite, event_async_disabled_cutoff_fail_b, dfix_empty);
	ptu_run_fp(suite, event_async_branch, dfix_empty, pt_ipc_suppressed, 0);
	ptu_run_fp(suite, event_async_branch, dfix_empty, pt_ipc_update_16, 0);
	ptu_run_fp(suite, event_async_branch, dfix_empty, pt_ipc_update_32, 0);
	ptu_run_fp(suite, event_async_branch, dfix_empty, pt_ipc_update_48, 0);
	ptu_run_fp(suite, event_async_branch, dfix_empty, pt_ipc_sext_48, 0);
	ptu_run_fp(suite, event_async_branch, dfix_empty, pt_ipc_full, 0);
	ptu_run_f(suite, event_async_branch_suppressed_fail, dfix_empty);
	ptu_run_f(suite, event_async_branch_cutoff_fail_a, dfix_empty);
	ptu_run_f(suite, event_async_branch_cutoff_fail_b, dfix_empty);
	ptu_run_fp(suite, event_paging, dfix_empty, 0, 0);
	ptu_run_fp(suite, event_paging, dfix_empty, pt_pl_pip_nr, 0);
	ptu_run_f(suite, event_paging_cutoff_fail, dfix_empty);
	ptu_run_fp(suite, event_async_paging, dfix_empty, 0, 0);
	ptu_run_fp(suite, event_async_paging, dfix_empty, pt_pl_pip_nr, 0);
	ptu_run_fp(suite, event_async_paging_suppressed, dfix_empty, 0, 0);
	ptu_run_fp(suite, event_async_paging_suppressed, dfix_empty,
		   pt_pl_pip_nr, 0);
	ptu_run_f(suite, event_async_paging_cutoff_fail, dfix_empty);
	ptu_run_fp(suite, event_overflow_fup, dfix_empty, pt_ipc_suppressed, 0);
	ptu_run_fp(suite, event_overflow_fup, dfix_empty, pt_ipc_update_16, 0);
	ptu_run_fp(suite, event_overflow_fup, dfix_empty, pt_ipc_update_32, 0);
	ptu_run_fp(suite, event_overflow_fup, dfix_empty, pt_ipc_update_48, 0);
	ptu_run_fp(suite, event_overflow_fup, dfix_empty, pt_ipc_sext_48, 0);
	ptu_run_fp(suite, event_overflow_fup, dfix_empty, pt_ipc_full, 0);
	ptu_run_fp(suite, event_overflow_tip_pge, dfix_empty,
		   pt_ipc_suppressed, 0);
	ptu_run_fp(suite, event_overflow_tip_pge, dfix_empty, pt_ipc_update_16,
		   0);
	ptu_run_fp(suite, event_overflow_tip_pge, dfix_empty, pt_ipc_update_32,
		   0);
	ptu_run_fp(suite, event_overflow_tip_pge, dfix_empty, pt_ipc_update_48,
		   0);
	ptu_run_fp(suite, event_overflow_tip_pge, dfix_empty, pt_ipc_sext_48,
		   0);
	ptu_run_fp(suite, event_overflow_tip_pge, dfix_empty, pt_ipc_full,
		   0);
	ptu_run_f(suite, event_overflow_cutoff_fail, dfix_empty);
	ptu_run_fp(suite, event_stop, dfix_empty, 0);
	ptu_run_fp(suite, event_exec_mode_tip, dfix_empty, pt_ipc_suppressed,
		   0);
	ptu_run_fp(suite, event_exec_mode_tip, dfix_empty, pt_ipc_update_16, 0);
	ptu_run_fp(suite, event_exec_mode_tip, dfix_empty, pt_ipc_update_32, 0);
	ptu_run_fp(suite, event_exec_mode_tip, dfix_empty, pt_ipc_update_48, 0);
	ptu_run_fp(suite, event_exec_mode_tip, dfix_empty, pt_ipc_sext_48, 0);
	ptu_run_fp(suite, event_exec_mode_tip, dfix_empty, pt_ipc_full, 0);
	ptu_run_f(suite, event_exec_mode_tip_cutoff_fail, dfix_empty);
	ptu_run_fp(suite, event_exec_mode_tip_pge, dfix_empty,
		   pt_ipc_suppressed, 0);
	ptu_run_fp(suite, event_exec_mode_tip_pge, dfix_empty,
		   pt_ipc_update_16, 0);
	ptu_run_fp(suite, event_exec_mode_tip_pge, dfix_empty,
		   pt_ipc_update_32, 0);
	ptu_run_fp(suite, event_exec_mode_tip_pge, dfix_empty,
		   pt_ipc_update_48, 0);
	ptu_run_fp(suite, event_exec_mode_tip_pge, dfix_empty, pt_ipc_sext_48,
		   0);
	ptu_run_fp(suite, event_exec_mode_tip_pge, dfix_empty, pt_ipc_full,
		   0);
	ptu_run_f(suite, event_exec_mode_tip_pge_cutoff_fail, dfix_empty);
	ptu_run_f(suite, event_exec_mode_cutoff_fail, dfix_empty);
	ptu_run_fp(suite, event_tsx_fup, dfix_empty, pt_ipc_suppressed,
		   pt_mob_tsx_intx, 0);
	ptu_run_fp(suite, event_tsx_fup, dfix_empty, pt_ipc_update_16, 0, 0);
	ptu_run_fp(suite, event_tsx_fup, dfix_empty, pt_ipc_update_32,
		   pt_mob_tsx_intx, 0);
	ptu_run_fp(suite, event_tsx_fup, dfix_empty, pt_ipc_update_48,
		   pt_mob_tsx_intx, 0);
	ptu_run_fp(suite, event_tsx_fup, dfix_empty, pt_ipc_sext_48, 0, 0);
	ptu_run_fp(suite, event_tsx_fup, dfix_empty, pt_ipc_full, 0, 0);
	ptu_run_f(suite, event_tsx_fup_cutoff_fail, dfix_empty);
	ptu_run_f(suite, event_tsx_cutoff_fail, dfix_empty);
	ptu_run_f(suite, event_skip_tip_fail, dfix_empty);
	ptu_run_f(suite, event_skip_tnt_8_fail, dfix_empty);
	ptu_run_f(suite, event_skip_tnt_64_fail, dfix_empty);
	ptu_run_fp(suite, sync_event, dfix_empty, pt_ipc_suppressed);
	ptu_run_fp(suite, sync_event, dfix_empty, pt_ipc_update_16);
	ptu_run_fp(suite, sync_event, dfix_empty, pt_ipc_update_32);
	ptu_run_fp(suite, sync_event, dfix_empty, pt_ipc_update_48);
	ptu_run_fp(suite, sync_event, dfix_empty, pt_ipc_sext_48);
	ptu_run_fp(suite, sync_event, dfix_empty, pt_ipc_full);
	ptu_run_f(suite, sync_event_cutoff_fail, dfix_empty);
	ptu_run_f(suite, sync_event_incomplete_fail, dfix_empty);
	ptu_run_fp(suite, sync_ovf_event, dfix_empty, pt_ipc_suppressed);
	ptu_run_fp(suite, sync_ovf_event, dfix_empty, pt_ipc_update_16);
	ptu_run_fp(suite, sync_ovf_event, dfix_empty, pt_ipc_update_32);
	ptu_run_fp(suite, sync_ovf_event, dfix_empty, pt_ipc_update_48);
	ptu_run_fp(suite, sync_ovf_event, dfix_empty, pt_ipc_sext_48);
	ptu_run_fp(suite, sync_ovf_event, dfix_empty, pt_ipc_full);
	ptu_run_f(suite, sync_ovf_event_cutoff_fail, dfix_empty);

	ptu_run_fp(suite, event_enabled, dfix_event, pt_ipc_suppressed, 0x1000);
	ptu_run_fp(suite, event_enabled, dfix_event, pt_ipc_update_16, 0x1000);
	ptu_run_fp(suite, event_enabled, dfix_event, pt_ipc_update_32, 0x1000);
	ptu_run_fp(suite, event_enabled, dfix_event, pt_ipc_update_48, 0x1000);
	ptu_run_fp(suite, event_enabled, dfix_event, pt_ipc_sext_48, 0x1000);
	ptu_run_fp(suite, event_enabled, dfix_event, pt_ipc_full, 0x1000);
	ptu_run_f(suite, event_enabled_cutoff_fail, dfix_event);
	ptu_run_fp(suite, event_disabled, dfix_event, pt_ipc_suppressed,
		   0x1000);
	ptu_run_fp(suite, event_disabled, dfix_event, pt_ipc_update_16, 0x1000);
	ptu_run_fp(suite, event_disabled, dfix_event, pt_ipc_update_32, 0x1000);
	ptu_run_fp(suite, event_disabled, dfix_event, pt_ipc_update_48, 0x1000);
	ptu_run_fp(suite, event_disabled, dfix_event, pt_ipc_sext_48, 0x1000);
	ptu_run_fp(suite, event_disabled, dfix_event, pt_ipc_full, 0x1000);
	ptu_run_f(suite, event_disabled_cutoff_fail, dfix_event);
	ptu_run_fp(suite, event_async_disabled, dfix_event, pt_ipc_suppressed,
		   0x1000);
	ptu_run_fp(suite, event_async_disabled, dfix_event, pt_ipc_update_16,
		   0x1000);
	ptu_run_fp(suite, event_async_disabled, dfix_event, pt_ipc_update_32,
		   0x1000);
	ptu_run_fp(suite, event_async_disabled, dfix_event, pt_ipc_update_48,
		   0x1000);
	ptu_run_fp(suite, event_async_disabled, dfix_event, pt_ipc_sext_48,
		   0x1000);
	ptu_run_fp(suite, event_async_disabled, dfix_event, pt_ipc_full,
		   0x1000);
	ptu_run_f(suite, event_async_disabled_suppressed_fail, dfix_event);
	ptu_run_f(suite, event_async_disabled_cutoff_fail_a, dfix_event);
	ptu_run_f(suite, event_async_disabled_cutoff_fail_b, dfix_event);
	ptu_run_fp(suite, event_async_branch, dfix_event, pt_ipc_suppressed,
		   0x1000);
	ptu_run_fp(suite, event_async_branch, dfix_event, pt_ipc_update_16,
		   0x1000);
	ptu_run_fp(suite, event_async_branch, dfix_event, pt_ipc_update_32,
		   0x1000);
	ptu_run_fp(suite, event_async_branch, dfix_event, pt_ipc_update_48,
		   0x1000);
	ptu_run_fp(suite, event_async_branch, dfix_event, pt_ipc_sext_48,
		   0x1000);
	ptu_run_fp(suite, event_async_branch, dfix_event, pt_ipc_full,
		   0x1000);
	ptu_run_f(suite, event_async_branch_suppressed_fail, dfix_event);
	ptu_run_f(suite, event_async_branch_cutoff_fail_a, dfix_event);
	ptu_run_f(suite, event_async_branch_cutoff_fail_b, dfix_event);
	ptu_run_fp(suite, event_paging, dfix_event, 0, 0x1000);
	ptu_run_fp(suite, event_paging, dfix_event, pt_pl_pip_nr, 0x1000);
	ptu_run_f(suite, event_paging_cutoff_fail, dfix_event);
	ptu_run_fp(suite, event_async_paging, dfix_event, 0, 0x1000);
	ptu_run_fp(suite, event_async_paging, dfix_event, pt_pl_pip_nr, 0x1000);
	ptu_run_fp(suite, event_async_paging_suppressed, dfix_event, 0, 0x1000);
	ptu_run_fp(suite, event_async_paging_suppressed, dfix_event,
		   pt_pl_pip_nr, 0x1000);
	ptu_run_f(suite, event_async_paging_cutoff_fail, dfix_event);
	ptu_run_fp(suite, event_overflow_fup, dfix_event, pt_ipc_suppressed,
		   0x1000);
	ptu_run_fp(suite, event_overflow_fup, dfix_event, pt_ipc_update_16,
		   0x1000);
	ptu_run_fp(suite, event_overflow_fup, dfix_event, pt_ipc_update_32,
		   0x1000);
	ptu_run_fp(suite, event_overflow_fup, dfix_event, pt_ipc_update_48,
		   0x1000);
	ptu_run_fp(suite, event_overflow_fup, dfix_event, pt_ipc_sext_48,
		   0x1000);
	ptu_run_fp(suite, event_overflow_fup, dfix_event, pt_ipc_full,
		   0x1000);
	ptu_run_fp(suite, event_overflow_tip_pge, dfix_event,
		   pt_ipc_suppressed, 0x1000);
	ptu_run_fp(suite, event_overflow_tip_pge, dfix_event, pt_ipc_update_16,
		   0x1000);
	ptu_run_fp(suite, event_overflow_tip_pge, dfix_event, pt_ipc_update_32,
		   0x1000);
	ptu_run_fp(suite, event_overflow_tip_pge, dfix_event, pt_ipc_update_48,
		   0x1000);
	ptu_run_fp(suite, event_overflow_tip_pge, dfix_event, pt_ipc_sext_48,
		   0x1000);
	ptu_run_fp(suite, event_overflow_tip_pge, dfix_event, pt_ipc_full,
		   0x1000);
	ptu_run_f(suite, event_overflow_cutoff_fail, dfix_event);
	ptu_run_fp(suite, event_stop, dfix_event, 0x1000);
	ptu_run_fp(suite, event_exec_mode_tip, dfix_event, pt_ipc_suppressed,
		   0x1000);
	ptu_run_fp(suite, event_exec_mode_tip, dfix_event, pt_ipc_update_16,
		   0x1000);
	ptu_run_fp(suite, event_exec_mode_tip, dfix_event, pt_ipc_update_32,
		   0x1000);
	ptu_run_fp(suite, event_exec_mode_tip, dfix_event, pt_ipc_update_48,
		   0x1000);
	ptu_run_fp(suite, event_exec_mode_tip, dfix_event, pt_ipc_sext_48,
		   0x1000);
	ptu_run_fp(suite, event_exec_mode_tip, dfix_event, pt_ipc_full,
		   0x1000);
	ptu_run_f(suite, event_exec_mode_tip_cutoff_fail, dfix_event);
	ptu_run_fp(suite, event_exec_mode_tip_pge, dfix_event,
		   pt_ipc_suppressed, 0x1000);
	ptu_run_fp(suite, event_exec_mode_tip_pge, dfix_event,
		   pt_ipc_update_16, 0x1000);
	ptu_run_fp(suite, event_exec_mode_tip_pge, dfix_event,
		   pt_ipc_update_32, 0x1000);
	ptu_run_fp(suite, event_exec_mode_tip_pge, dfix_event,
		   pt_ipc_update_48, 0x1000);
	ptu_run_fp(suite, event_exec_mode_tip_pge, dfix_event, pt_ipc_sext_48,
		   0x1000);
	ptu_run_fp(suite, event_exec_mode_tip_pge, dfix_event, pt_ipc_full,
		   0x1000);
	ptu_run_f(suite, event_exec_mode_tip_pge_cutoff_fail, dfix_event);
	ptu_run_f(suite, event_exec_mode_cutoff_fail, dfix_event);
	ptu_run_fp(suite, event_tsx_fup, dfix_event, pt_ipc_suppressed, 0,
		   0x1000);
	ptu_run_fp(suite, event_tsx_fup, dfix_event, pt_ipc_update_16,
		   pt_mob_tsx_intx, 0x1000);
	ptu_run_fp(suite, event_tsx_fup, dfix_event, pt_ipc_update_32, 0,
		   0x1000);
	ptu_run_fp(suite, event_tsx_fup, dfix_event, pt_ipc_update_48, 0,
		   0x1000);
	ptu_run_fp(suite, event_tsx_fup, dfix_event, pt_ipc_sext_48,
		   pt_mob_tsx_intx, 0x1000);
	ptu_run_fp(suite, event_tsx_fup, dfix_event, pt_ipc_full,
		   pt_mob_tsx_intx, 0x1000);
	ptu_run_f(suite, event_tsx_fup_cutoff_fail, dfix_event);
	ptu_run_f(suite, event_tsx_cutoff_fail, dfix_event);
	ptu_run_f(suite, event_skip_tip_fail, dfix_event);
	ptu_run_f(suite, event_skip_tnt_8_fail, dfix_event);
	ptu_run_f(suite, event_skip_tnt_64_fail, dfix_event);
	ptu_run_fp(suite, sync_event, dfix_event, pt_ipc_suppressed);
	ptu_run_fp(suite, sync_event, dfix_event, pt_ipc_update_16);
	ptu_run_fp(suite, sync_event, dfix_event, pt_ipc_update_32);
	ptu_run_fp(suite, sync_event, dfix_event, pt_ipc_update_48);
	ptu_run_fp(suite, sync_event, dfix_event, pt_ipc_sext_48);
	ptu_run_fp(suite, sync_event, dfix_event, pt_ipc_full);
	ptu_run_f(suite, sync_event_cutoff_fail, dfix_event);
	ptu_run_f(suite, sync_event_incomplete_fail, dfix_event);
	ptu_run_fp(suite, sync_ovf_event, dfix_event, pt_ipc_suppressed);
	ptu_run_fp(suite, sync_ovf_event, dfix_event, pt_ipc_update_16);
	ptu_run_fp(suite, sync_ovf_event, dfix_event, pt_ipc_update_32);
	ptu_run_fp(suite, sync_ovf_event, dfix_event, pt_ipc_update_48);
	ptu_run_fp(suite, sync_ovf_event, dfix_event, pt_ipc_sext_48);
	ptu_run_fp(suite, sync_ovf_event, dfix_event, pt_ipc_full);
	ptu_run_f(suite, sync_ovf_event_cutoff_fail, dfix_event);

	ptu_run_fp(suite, event_enabled, dfix_event_psb, pt_ipc_suppressed,
		   0x1000);
	ptu_run_fp(suite, event_enabled, dfix_event_psb, pt_ipc_sext_48,
		   0x1000);
	ptu_run_fp(suite, event_enabled, dfix_event_psb, pt_ipc_full,
		   0x1000);
	ptu_run_f(suite, event_enabled_cutoff_fail, dfix_event_psb);
	ptu_run_fp(suite, event_disabled, dfix_event_psb, pt_ipc_suppressed,
		   0x1000);
	ptu_run_fp(suite, event_disabled, dfix_event_psb, pt_ipc_sext_48,
		   0x1000);
	ptu_run_fp(suite, event_disabled, dfix_event_psb, pt_ipc_full,
		   0x1000);
	ptu_run_f(suite, event_disabled_cutoff_fail, dfix_event_psb);
	ptu_run_fp(suite, event_async_disabled, dfix_event_psb,
		   pt_ipc_suppressed, 0x1000);
	ptu_run_fp(suite, event_async_disabled, dfix_event_psb,
		   pt_ipc_update_16, 0x1000);
	ptu_run_fp(suite, event_async_disabled, dfix_event_psb,
		   pt_ipc_update_32, 0x1000);
	ptu_run_fp(suite, event_async_disabled, dfix_event_psb,
		   pt_ipc_update_48, 0x1000);
	ptu_run_fp(suite, event_async_disabled, dfix_event_psb,
		   pt_ipc_sext_48, 0x1000);
	ptu_run_fp(suite, event_async_disabled, dfix_event_psb,
		   pt_ipc_full, 0x1000);
	ptu_run_f(suite, event_async_disabled_suppressed_fail, dfix_event_psb);
	ptu_run_f(suite, event_async_disabled_cutoff_fail_a, dfix_event_psb);
	ptu_run_f(suite, event_async_disabled_cutoff_fail_b, dfix_event_psb);
	ptu_run_fp(suite, event_async_branch, dfix_event_psb,
		   pt_ipc_suppressed, 0x1000);
	ptu_run_fp(suite, event_async_branch, dfix_event_psb, pt_ipc_update_16,
		   0x1000);
	ptu_run_fp(suite, event_async_branch, dfix_event_psb, pt_ipc_update_32,
		   0x1000);
	ptu_run_fp(suite, event_async_branch, dfix_event_psb, pt_ipc_update_48,
		   0x1000);
	ptu_run_fp(suite, event_async_branch, dfix_event_psb, pt_ipc_sext_48,
		   0x1000);
	ptu_run_fp(suite, event_async_branch, dfix_event_psb, pt_ipc_full,
		   0x1000);
	ptu_run_f(suite, event_async_branch_suppressed_fail, dfix_event_psb);
	ptu_run_f(suite, event_async_branch_cutoff_fail_a, dfix_event_psb);
	ptu_run_f(suite, event_async_branch_cutoff_fail_b, dfix_event_psb);
	ptu_run_fp(suite, event_paging, dfix_event_psb, 0, 0x1000);
	ptu_run_fp(suite, event_paging, dfix_event_psb, pt_pl_pip_nr, 0x1000);
	ptu_run_f(suite, event_paging_cutoff_fail, dfix_event_psb);
	ptu_run_fp(suite, event_async_paging, dfix_event_psb, 0, 0x1000);
	ptu_run_fp(suite, event_async_paging, dfix_event_psb, pt_pl_pip_nr,
		   0x1000);
	ptu_run_fp(suite, event_async_paging_suppressed, dfix_event_psb, 0,
		   0x1000);
	ptu_run_fp(suite, event_async_paging_suppressed, dfix_event_psb,
		  pt_pl_pip_nr, 0x1000);
	ptu_run_f(suite, event_async_paging_cutoff_fail, dfix_event_psb);
	ptu_run_f(suite, event_overflow_cutoff_fail, dfix_event_psb);
	ptu_run_fp(suite, event_stop, dfix_event_psb, 0x1000);
	ptu_run_fp(suite, event_exec_mode_tip, dfix_event_psb,
		   pt_ipc_suppressed, 0x1000);
	ptu_run_fp(suite, event_exec_mode_tip, dfix_event_psb, pt_ipc_sext_48,
		   0x1000);
	ptu_run_fp(suite, event_exec_mode_tip, dfix_event_psb, pt_ipc_full,
		   0x1000);
	ptu_run_f(suite, event_exec_mode_tip_cutoff_fail, dfix_event_psb);
	ptu_run_fp(suite, event_exec_mode_tip_pge, dfix_event_psb,
		   pt_ipc_sext_48, 0x1000);
	ptu_run_fp(suite, event_exec_mode_tip_pge, dfix_event_psb,
		   pt_ipc_full, 0x1000);
	ptu_run_f(suite, event_exec_mode_tip_pge_cutoff_fail, dfix_event_psb);
	ptu_run_f(suite, event_exec_mode_cutoff_fail, dfix_event_psb);
	ptu_run_fp(suite, event_tsx_fup, dfix_event_psb, pt_ipc_suppressed, 0,
		   0x1000);
	ptu_run_fp(suite, event_tsx_fup, dfix_event_psb, pt_ipc_sext_48,
		   pt_mob_tsx_intx, 0x1000);
	ptu_run_fp(suite, event_tsx_fup, dfix_event_psb, pt_ipc_full,
		   pt_mob_tsx_intx, 0x1000);
	ptu_run_f(suite, event_tsx_fup_cutoff_fail, dfix_event_psb);
	ptu_run_f(suite, event_tsx_cutoff_fail, dfix_event_psb);
	ptu_run_f(suite, event_skip_tip_fail, dfix_event_psb);
	ptu_run_f(suite, event_skip_tnt_8_fail, dfix_event_psb);
	ptu_run_f(suite, event_skip_tnt_64_fail, dfix_event_psb);

	ptu_run_f(suite, time_null_fail, dfix_empty);
	ptu_run_f(suite, time_initial, dfix_empty);
	ptu_run_f(suite, time, dfix_empty);

	ptu_run_f(suite, cbr_null, dfix_empty);
	ptu_run_f(suite, cbr_initial, dfix_empty);
	ptu_run_f(suite, cbr, dfix_empty);

	ptu_run_f(suite, indir_cyc_cutoff, dfix_empty);
	ptu_run_f(suite, cond_cyc_cutoff, dfix_empty);
	ptu_run_f(suite, event_cyc_cutoff, dfix_empty);

	return ptunit_report(&suite);
}
