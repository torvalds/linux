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

#include "ptunit.h"

#include "pt_packet_decoder.h"
#include "pt_query_decoder.h"
#include "pt_encoder.h"
#include "pt_opcodes.h"

#include "intel-pt.h"

#include <string.h>


/* A test fixture providing everything needed for packet en- and de-coding. */
struct packet_fixture {
	/* The trace buffer. */
	uint8_t buffer[64];

	/* Two packets for encoding[0] and decoding[1]. */
	struct pt_packet packet[2];

	/* The configuration. */
	struct pt_config config;

	/* The encoder. */
	struct pt_encoder encoder;

	/* The decoder. */
	struct pt_packet_decoder decoder;

	/* The return value for an unknown decode. */
	int unknown;

	/* The test fixture initialization and finalization functions. */
	struct ptunit_result (*init)(struct packet_fixture *);
	struct ptunit_result (*fini)(struct packet_fixture *);
};

static int pfix_decode_unknown(struct pt_packet_unknown *packet,
			       const struct pt_config *config,
			       const uint8_t *pos, void *context)
{
	struct packet_fixture *pfix;

	if (!packet || !config)
		return -pte_internal;

	pfix = (struct packet_fixture *) context;
	if (!pfix)
		return -pte_internal;

	if (config->begin != pfix->buffer)
		return -pte_internal;

	if (config->end != pfix->buffer + sizeof(pfix->buffer))
		return -pte_internal;

	if (pos != pfix->buffer)
		return -pte_internal;

	packet->priv = pfix;

	return pfix->unknown;
}

static struct ptunit_result pfix_init(struct packet_fixture *pfix)
{
	int errcode;

	memset(pfix->buffer, 0, sizeof(pfix->buffer));
	memset(pfix->packet, 0, sizeof(pfix->packet));
	memset(&pfix->config, 0, sizeof(pfix->config));
	pfix->config.size = sizeof(pfix->config);
	pfix->config.begin = pfix->buffer;
	pfix->config.end = pfix->buffer + sizeof(pfix->buffer);
	pfix->config.decode.callback = pfix_decode_unknown;
	pfix->config.decode.context = pfix;

	pt_encoder_init(&pfix->encoder, &pfix->config);
	pt_pkt_decoder_init(&pfix->decoder, &pfix->config);

	errcode = pt_pkt_sync_set(&pfix->decoder, 0x0ull);
	ptu_int_eq(errcode, 0);

	pfix->unknown = 0;

	return ptu_passed();
}

static struct ptunit_result pfix_fini(struct packet_fixture *pfix)
{
	pt_encoder_fini(&pfix->encoder);
	pt_pkt_decoder_fini(&pfix->decoder);

	return ptu_passed();
}

static struct ptunit_result ptu_pkt_eq(const struct pt_packet *enc,
				       const struct pt_packet *dec)
{
	const uint8_t *renc, *rdec;
	size_t byte;

	ptu_ptr(enc);
	ptu_ptr(dec);

	renc = (const uint8_t *) enc;
	rdec = (const uint8_t *) dec;

	for (byte = 0; byte < sizeof(*enc); ++byte)
		ptu_uint_eq(renc[byte], rdec[byte]);

	return ptu_passed();
}

static struct ptunit_result pfix_test(struct packet_fixture *pfix)
{
	int size;

	size = pt_enc_next(&pfix->encoder, &pfix->packet[0]);
	ptu_int_gt(size, 0);

	pfix->packet[0].size = (uint8_t) size;

	size = pt_pkt_next(&pfix->decoder, &pfix->packet[1],
			   sizeof(pfix->packet[1]));
	ptu_int_gt(size, 0);

	return ptu_pkt_eq(&pfix->packet[0], &pfix->packet[1]);
}

static struct ptunit_result no_payload(struct packet_fixture *pfix,
				       enum pt_packet_type type)
{
	pfix->packet[0].type = type;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result unknown(struct packet_fixture *pfix, int exp)
{
	int size;

	pfix->buffer[0] = pt_opc_bad;
	pfix->unknown = exp;

	size = pt_pkt_next(&pfix->decoder, &pfix->packet[1],
			   sizeof(pfix->packet[1]));
	ptu_int_eq(size, pfix->unknown);

	if (size >= 0) {
		ptu_int_eq(pfix->packet[1].type, ppt_unknown);
		ptu_uint_eq(pfix->packet[1].size, (uint8_t) size);
		ptu_ptr_eq(pfix->packet[1].payload.unknown.packet,
			   pfix->buffer);
		ptu_ptr_eq(pfix->packet[1].payload.unknown.priv, pfix);
	}

	return ptu_passed();
}

static struct ptunit_result unknown_ext(struct packet_fixture *pfix, int exp)
{
	int size;

	pfix->buffer[0] = pt_opc_ext;
	pfix->buffer[1] = pt_ext_bad;
	pfix->unknown = exp;

	size = pt_pkt_next(&pfix->decoder, &pfix->packet[1],
			   sizeof(pfix->packet[1]));
	ptu_int_eq(size, pfix->unknown);

	if (size >= 0) {
		ptu_int_eq(pfix->packet[1].type, ppt_unknown);
		ptu_uint_eq(pfix->packet[1].size, (uint8_t) size);
		ptu_ptr_eq(pfix->packet[1].payload.unknown.packet,
			   pfix->buffer);
		ptu_ptr_eq(pfix->packet[1].payload.unknown.priv, pfix);
	}

	return ptu_passed();
}

static struct ptunit_result unknown_ext2(struct packet_fixture *pfix, int exp)
{
	int size;

	pfix->buffer[0] = pt_opc_ext;
	pfix->buffer[1] = pt_ext_ext2;
	pfix->buffer[2] = pt_ext2_bad;
	pfix->unknown = exp;

	size = pt_pkt_next(&pfix->decoder, &pfix->packet[1],
			   sizeof(pfix->packet[1]));
	ptu_int_eq(size, exp);

	if (exp >= 0) {
		ptu_int_eq(pfix->packet[1].type, ppt_unknown);
		ptu_uint_eq(pfix->packet[1].size, (uint8_t) size);
		ptu_ptr_eq(pfix->packet[1].payload.unknown.packet,
			   pfix->buffer);
		ptu_ptr_eq(pfix->packet[1].payload.unknown.priv, pfix);
	}

	return ptu_passed();
}

static struct ptunit_result tnt_8(struct packet_fixture *pfix)
{
	pfix->packet[0].type = ppt_tnt_8;
	pfix->packet[0].payload.tnt.bit_size = 4;
	pfix->packet[0].payload.tnt.payload = 0x5ull;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result tnt_64(struct packet_fixture *pfix)
{
	pfix->packet[0].type = ppt_tnt_64;
	pfix->packet[0].payload.tnt.bit_size = 23;
	pfix->packet[0].payload.tnt.payload = 0xabcdeull;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result ip(struct packet_fixture *pfix,
			       enum pt_packet_type type,
			       enum pt_ip_compression ipc,
			       uint64_t ip)
{
	pfix->packet[0].type = type;
	pfix->packet[0].payload.ip.ipc = ipc;
	pfix->packet[0].payload.ip.ip = ip;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result mode_exec(struct packet_fixture *pfix,
				      enum pt_exec_mode mode)
{
	struct pt_packet_mode_exec packet;

	packet = pt_set_exec_mode(mode);

	pfix->packet[0].type = ppt_mode;
	pfix->packet[0].payload.mode.leaf = pt_mol_exec;
	pfix->packet[0].payload.mode.bits.exec.csl = packet.csl;
	pfix->packet[0].payload.mode.bits.exec.csd = packet.csd;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result mode_tsx(struct packet_fixture *pfix)
{
	pfix->packet[0].type = ppt_mode;
	pfix->packet[0].payload.mode.leaf = pt_mol_tsx;
	pfix->packet[0].payload.mode.bits.tsx.intx = 1;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result pip(struct packet_fixture *pfix)
{
	pfix->packet[0].type = ppt_pip;
	pfix->packet[0].payload.pip.cr3 = 0x4200ull;
	pfix->packet[0].payload.pip.nr = 1;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result tsc(struct packet_fixture *pfix)
{
	pfix->packet[0].type = ppt_tsc;
	pfix->packet[0].payload.tsc.tsc = 0x42ull;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result cbr(struct packet_fixture *pfix)
{
	pfix->packet[0].type = ppt_cbr;
	pfix->packet[0].payload.cbr.ratio = 0x23;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result tma(struct packet_fixture *pfix)
{
	pfix->packet[0].type = ppt_tma;
	pfix->packet[0].payload.tma.ctc = 0x42;
	pfix->packet[0].payload.tma.fc = 0x123;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result tma_bad(struct packet_fixture *pfix)
{
	int errcode;

	pfix->packet[0].type = ppt_tma;
	pfix->packet[0].payload.tma.ctc = 0x42;
	pfix->packet[0].payload.tma.fc = 0x200;

	errcode = pt_enc_next(&pfix->encoder, &pfix->packet[0]);
	ptu_int_eq(errcode, -pte_bad_packet);

	return ptu_passed();
}

static struct ptunit_result mtc(struct packet_fixture *pfix)
{
	pfix->packet[0].type = ppt_mtc;
	pfix->packet[0].payload.mtc.ctc = 0x23;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result cyc(struct packet_fixture *pfix)
{
	pfix->packet[0].type = ppt_cyc;
	pfix->packet[0].payload.cyc.value = 0x23;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result vmcs(struct packet_fixture *pfix)
{
	pfix->packet[0].type = ppt_vmcs;
	pfix->packet[0].payload.vmcs.base = 0xabcdef000ull;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result mnt(struct packet_fixture *pfix)
{
	pfix->packet[0].type = ppt_mnt;
	pfix->packet[0].payload.mnt.payload = 0x1234567890abcdefull;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result exstop(struct packet_fixture *pfix, int ip)
{
	pfix->packet[0].type = ppt_exstop;
	pfix->packet[0].payload.exstop.ip = ip ? 1 : 0;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result mwait(struct packet_fixture *pfix)
{
	pfix->packet[0].type = ppt_mwait;
	pfix->packet[0].payload.mwait.hints = 0xc;
	pfix->packet[0].payload.mwait.ext = 0x1;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result pwre(struct packet_fixture *pfix)
{
	pfix->packet[0].type = ppt_pwre;
	pfix->packet[0].payload.pwre.state = 0x0;
	pfix->packet[0].payload.pwre.sub_state = 0x3;
	pfix->packet[0].payload.pwre.hw = 1;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result pwrx(struct packet_fixture *pfix)
{
	pfix->packet[0].type = ppt_pwrx;
	pfix->packet[0].payload.pwrx.last = 0x3;
	pfix->packet[0].payload.pwrx.deepest = 0xa;
	pfix->packet[0].payload.pwrx.store = 1;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result ptw(struct packet_fixture *pfix, uint8_t plc,
				int ip)
{
	uint64_t pl, mask;
	int size;

	size = pt_ptw_size(plc);
	ptu_int_gt(size, 0);

	pl = 0x1234567890abcdefull;

	ptu_uint_le((size_t) size, sizeof(mask));
	mask = ~0ull >> ((sizeof(mask) - (size_t) size) * 8);

	pfix->packet[0].type = ppt_ptw;
	pfix->packet[0].payload.ptw.payload = pl & mask;
	pfix->packet[0].payload.ptw.plc = plc;
	pfix->packet[0].payload.ptw.ip = ip ? 1 : 0;

	ptu_test(pfix_test, pfix);

	return ptu_passed();
}

static struct ptunit_result cutoff(struct packet_fixture *pfix,
				   enum pt_packet_type type)
{
	int size;

	pfix->packet[0].type = type;

	size = pt_enc_next(&pfix->encoder, &pfix->packet[0]);
	ptu_int_gt(size, 0);

	pfix->decoder.config.end = pfix->encoder.pos - 1;

	size = pt_pkt_next(&pfix->decoder, &pfix->packet[1],
			   sizeof(pfix->packet[1]));
	ptu_int_eq(size, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result cutoff_ip(struct packet_fixture *pfix,
				      enum pt_packet_type type)
{
	int size;

	pfix->packet[0].type = type;
	pfix->packet[0].payload.ip.ipc = pt_ipc_sext_48;

	size = pt_enc_next(&pfix->encoder, &pfix->packet[0]);
	ptu_int_gt(size, 0);

	pfix->decoder.config.end = pfix->encoder.pos - 1;

	size = pt_pkt_next(&pfix->decoder, &pfix->packet[1],
			   sizeof(pfix->packet[1]));
	ptu_int_eq(size, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result cutoff_cyc(struct packet_fixture *pfix)
{
	int size;

	pfix->packet[0].type = ppt_cyc;
	pfix->packet[0].payload.cyc.value = 0xa8;

	size = pt_enc_next(&pfix->encoder, &pfix->packet[0]);
	ptu_int_gt(size, 0);

	pfix->decoder.config.end = pfix->encoder.pos - 1;

	size = pt_pkt_next(&pfix->decoder, &pfix->packet[1],
			   sizeof(pfix->packet[1]));
	ptu_int_eq(size, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result cutoff_mode(struct packet_fixture *pfix,
					enum pt_mode_leaf leaf)
{
	int size;

	pfix->packet[0].type = ppt_mode;
	pfix->packet[0].payload.mode.leaf = leaf;

	size = pt_enc_next(&pfix->encoder, &pfix->packet[0]);
	ptu_int_gt(size, 0);

	pfix->decoder.config.end = pfix->encoder.pos - 1;

	size = pt_pkt_next(&pfix->decoder, &pfix->packet[1],
			   sizeof(pfix->packet[1]));
	ptu_int_eq(size, -pte_eos);

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct packet_fixture pfix;
	struct ptunit_suite suite;

	pfix.init = pfix_init;
	pfix.fini = pfix_fini;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run_fp(suite, no_payload, pfix, ppt_pad);
	ptu_run_fp(suite, no_payload, pfix, ppt_psb);
	ptu_run_fp(suite, no_payload, pfix, ppt_ovf);
	ptu_run_fp(suite, no_payload, pfix, ppt_psbend);
	ptu_run_fp(suite, no_payload, pfix, ppt_stop);

	ptu_run_fp(suite, unknown, pfix, 4);
	ptu_run_fp(suite, unknown, pfix, -pte_nomem);
	ptu_run_fp(suite, unknown_ext, pfix, 4);
	ptu_run_fp(suite, unknown_ext, pfix, -pte_nomem);
	ptu_run_fp(suite, unknown_ext2, pfix, 4);
	ptu_run_fp(suite, unknown_ext2, pfix, -pte_nomem);

	ptu_run_f(suite, tnt_8, pfix);
	ptu_run_f(suite, tnt_64, pfix);

	ptu_run_fp(suite, ip, pfix, ppt_tip, pt_ipc_suppressed, 0x0ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip, pt_ipc_update_16, 0x42ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip, pt_ipc_update_32, 0x4200ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip, pt_ipc_update_48, 0x4200ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip, pt_ipc_sext_48, 0x42ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip, pt_ipc_full, 0x42ull);

	ptu_run_fp(suite, ip, pfix, ppt_tip, pt_ipc_suppressed, 0x0ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip, pt_ipc_update_16, 0x42ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip, pt_ipc_update_32, 0x4200ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip, pt_ipc_update_48, 0x4200ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip, pt_ipc_sext_48, 0x42ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip, pt_ipc_full, 0x42ull);

	ptu_run_fp(suite, ip, pfix, ppt_tip_pge, pt_ipc_suppressed, 0x0ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip_pge, pt_ipc_update_16, 0x42ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip_pge, pt_ipc_update_32, 0x4200ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip_pge, pt_ipc_update_48, 0x4200ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip_pge, pt_ipc_sext_48, 0x42ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip_pge, pt_ipc_full, 0x42ull);

	ptu_run_fp(suite, ip, pfix, ppt_tip_pgd, pt_ipc_suppressed, 0x0ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip_pgd, pt_ipc_update_16, 0x42ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip_pgd, pt_ipc_update_32, 0x4200ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip_pgd, pt_ipc_update_48, 0x4200ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip_pgd, pt_ipc_sext_48, 0x42ull);
	ptu_run_fp(suite, ip, pfix, ppt_tip_pgd, pt_ipc_full, 0x42ull);

	ptu_run_fp(suite, ip, pfix, ppt_fup, pt_ipc_suppressed, 0x0ull);
	ptu_run_fp(suite, ip, pfix, ppt_fup, pt_ipc_update_16, 0x42ull);
	ptu_run_fp(suite, ip, pfix, ppt_fup, pt_ipc_update_32, 0x4200ull);
	ptu_run_fp(suite, ip, pfix, ppt_fup, pt_ipc_update_48, 0x4200ull);
	ptu_run_fp(suite, ip, pfix, ppt_fup, pt_ipc_sext_48, 0x42ull);
	ptu_run_fp(suite, ip, pfix, ppt_fup, pt_ipc_full, 0x42ull);

	ptu_run_fp(suite, mode_exec, pfix, ptem_16bit);
	ptu_run_fp(suite, mode_exec, pfix, ptem_32bit);
	ptu_run_fp(suite, mode_exec, pfix, ptem_64bit);
	ptu_run_f(suite, mode_tsx, pfix);

	ptu_run_f(suite, pip, pfix);
	ptu_run_f(suite, tsc, pfix);
	ptu_run_f(suite, cbr, pfix);
	ptu_run_f(suite, tma, pfix);
	ptu_run_f(suite, tma_bad, pfix);
	ptu_run_f(suite, mtc, pfix);
	ptu_run_f(suite, cyc, pfix);
	ptu_run_f(suite, vmcs, pfix);
	ptu_run_f(suite, mnt, pfix);
	ptu_run_fp(suite, exstop, pfix, 0);
	ptu_run_fp(suite, exstop, pfix, 1);
	ptu_run_f(suite, mwait, pfix);
	ptu_run_f(suite, pwre, pfix);
	ptu_run_f(suite, pwrx, pfix);
	ptu_run_fp(suite, ptw, pfix, 0, 1);
	ptu_run_fp(suite, ptw, pfix, 1, 0);

	ptu_run_fp(suite, cutoff, pfix, ppt_psb);
	ptu_run_fp(suite, cutoff_ip, pfix, ppt_tip);
	ptu_run_fp(suite, cutoff_ip, pfix, ppt_tip_pge);
	ptu_run_fp(suite, cutoff_ip, pfix, ppt_tip_pgd);
	ptu_run_fp(suite, cutoff_ip, pfix, ppt_fup);
	ptu_run_fp(suite, cutoff, pfix, ppt_ovf);
	ptu_run_fp(suite, cutoff, pfix, ppt_psbend);
	ptu_run_fp(suite, cutoff, pfix, ppt_tnt_64);
	ptu_run_fp(suite, cutoff, pfix, ppt_tsc);
	ptu_run_fp(suite, cutoff, pfix, ppt_cbr);
	ptu_run_fp(suite, cutoff, pfix, ppt_tma);
	ptu_run_fp(suite, cutoff, pfix, ppt_mtc);
	ptu_run_f(suite, cutoff_cyc, pfix);
	ptu_run_fp(suite, cutoff_mode, pfix, pt_mol_exec);
	ptu_run_fp(suite, cutoff_mode, pfix, pt_mol_tsx);
	ptu_run_fp(suite, cutoff, pfix, ppt_vmcs);
	ptu_run_fp(suite, cutoff, pfix, ppt_mnt);
	ptu_run_fp(suite, cutoff, pfix, ppt_exstop);
	ptu_run_fp(suite, cutoff, pfix, ppt_mwait);
	ptu_run_fp(suite, cutoff, pfix, ppt_pwre);
	ptu_run_fp(suite, cutoff, pfix, ppt_pwrx);
	ptu_run_fp(suite, cutoff, pfix, ppt_ptw);

	return ptunit_report(&suite);
}


/* Dummy decode functions to satisfy link dependencies.
 *
 * As a nice side-effect, we will know if we need to add more tests when
 * adding new decoder functions.
 */
struct pt_query_decoder;

int pt_qry_decode_unknown(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_pad(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_psb(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_tip(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_tnt_8(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_tnt_64(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_tip_pge(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_tip_pgd(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_fup(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_header_fup(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_pip(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_header_pip(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_ovf(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_mode(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_header_mode(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_psbend(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_tsc(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_header_tsc(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_cbr(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_header_cbr(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_tma(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_mtc(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_cyc(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_stop(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_vmcs(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_header_vmcs(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_mnt(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_header_mnt(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_exstop(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_mwait(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_pwre(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_pwrx(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
int pt_qry_decode_ptw(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
