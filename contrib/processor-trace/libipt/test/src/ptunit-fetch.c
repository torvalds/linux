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

#include "pt_decoder_function.h"
#include "pt_packet_decoder.h"
#include "pt_query_decoder.h"
#include "pt_encoder.h"
#include "pt_opcodes.h"

#include "intel-pt.h"


/* A test fixture for decoder function fetch tests. */
struct fetch_fixture {
	/* The trace buffer. */
	uint8_t buffer[1024];

	/* A trace configuration. */
	struct pt_config config;

	/* A trace encoder. */
	struct pt_encoder encoder;

	/* The test fixture initialization and finalization functions. */
	struct ptunit_result (*init)(struct fetch_fixture *);
	struct ptunit_result (*fini)(struct fetch_fixture *);
};

static struct ptunit_result ffix_init(struct fetch_fixture *ffix)
{
	memset(ffix->buffer, pt_opc_bad, sizeof(ffix->buffer));

	memset(&ffix->config, 0, sizeof(ffix->config));
	ffix->config.size = sizeof(ffix->config);
	ffix->config.begin = ffix->buffer;
	ffix->config.end = ffix->buffer + sizeof(ffix->buffer);

	pt_encoder_init(&ffix->encoder, &ffix->config);

	return ptu_passed();
}

static struct ptunit_result ffix_fini(struct fetch_fixture *ffix)
{
	pt_encoder_fini(&ffix->encoder);

	return ptu_passed();
}


static struct ptunit_result fetch_null(struct fetch_fixture *ffix)
{
	const struct pt_decoder_function *dfun;
	int errcode;

	errcode = pt_df_fetch(NULL, ffix->config.begin, &ffix->config);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_df_fetch(&dfun, NULL, &ffix->config);
	ptu_int_eq(errcode, -pte_nosync);

	errcode = pt_df_fetch(&dfun, ffix->config.begin, NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result fetch_empty(struct fetch_fixture *ffix)
{
	const struct pt_decoder_function *dfun;
	int errcode;

	errcode = pt_df_fetch(&dfun, ffix->config.end, &ffix->config);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result fetch_unknown(struct fetch_fixture *ffix)
{
	const struct pt_decoder_function *dfun;
	int errcode;

	ffix->config.begin[0] = pt_opc_bad;

	errcode = pt_df_fetch(&dfun, ffix->config.begin, &ffix->config);
	ptu_int_eq(errcode, 0);
	ptu_ptr_eq(dfun, &pt_decode_unknown);

	return ptu_passed();
}

static struct ptunit_result fetch_unknown_ext(struct fetch_fixture *ffix)
{
	const struct pt_decoder_function *dfun;
	int errcode;

	ffix->config.begin[0] = pt_opc_ext;
	ffix->config.begin[1] = pt_ext_bad;

	errcode = pt_df_fetch(&dfun, ffix->config.begin, &ffix->config);
	ptu_int_eq(errcode, 0);
	ptu_ptr_eq(dfun, &pt_decode_unknown);

	return ptu_passed();
}

static struct ptunit_result fetch_unknown_ext2(struct fetch_fixture *ffix)
{
	const struct pt_decoder_function *dfun;
	int errcode;

	ffix->config.begin[0] = pt_opc_ext;
	ffix->config.begin[1] = pt_ext_ext2;
	ffix->config.begin[2] = pt_ext2_bad;

	errcode = pt_df_fetch(&dfun, ffix->config.begin, &ffix->config);
	ptu_int_eq(errcode, 0);
	ptu_ptr_eq(dfun, &pt_decode_unknown);

	return ptu_passed();
}

static struct ptunit_result fetch_packet(struct fetch_fixture *ffix,
					 const struct pt_packet *packet,
					 const struct pt_decoder_function *df)
{
	const struct pt_decoder_function *dfun;
	int errcode;

	errcode = pt_enc_next(&ffix->encoder, packet);
	ptu_int_ge(errcode, 0);

	errcode = pt_df_fetch(&dfun, ffix->config.begin, &ffix->config);
	ptu_int_eq(errcode, 0);
	ptu_ptr_eq(dfun, df);

	return ptu_passed();
}

static struct ptunit_result fetch_type(struct fetch_fixture *ffix,
				       enum pt_packet_type type,
				       const struct pt_decoder_function *dfun)
{
	struct pt_packet packet;

	memset(&packet, 0, sizeof(packet));
	packet.type = type;

	ptu_test(fetch_packet, ffix, &packet, dfun);

	return ptu_passed();
}

static struct ptunit_result fetch_tnt_8(struct fetch_fixture *ffix)
{
	struct pt_packet packet;

	memset(&packet, 0, sizeof(packet));
	packet.type = ppt_tnt_8;
	packet.payload.tnt.bit_size = 1;

	ptu_test(fetch_packet, ffix, &packet, &pt_decode_tnt_8);

	return ptu_passed();
}

static struct ptunit_result fetch_mode_exec(struct fetch_fixture *ffix)
{
	struct pt_packet packet;

	memset(&packet, 0, sizeof(packet));
	packet.type = ppt_mode;
	packet.payload.mode.leaf = pt_mol_exec;

	ptu_test(fetch_packet, ffix, &packet, &pt_decode_mode);

	return ptu_passed();
}

static struct ptunit_result fetch_mode_tsx(struct fetch_fixture *ffix)
{
	struct pt_packet packet;

	memset(&packet, 0, sizeof(packet));
	packet.type = ppt_mode;
	packet.payload.mode.leaf = pt_mol_tsx;

	ptu_test(fetch_packet, ffix, &packet, &pt_decode_mode);

	return ptu_passed();
}

static struct ptunit_result fetch_exstop_ip(struct fetch_fixture *ffix)
{
	struct pt_packet packet;

	memset(&packet, 0, sizeof(packet));
	packet.type = ppt_exstop;
	packet.payload.exstop.ip = 1;

	ptu_test(fetch_packet, ffix, &packet, &pt_decode_exstop);

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct fetch_fixture ffix;
	struct ptunit_suite suite;

	ffix.init = ffix_init;
	ffix.fini = ffix_fini;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run_f(suite, fetch_null, ffix);
	ptu_run_f(suite, fetch_empty, ffix);

	ptu_run_f(suite, fetch_unknown, ffix);
	ptu_run_f(suite, fetch_unknown_ext, ffix);
	ptu_run_f(suite, fetch_unknown_ext2, ffix);

	ptu_run_fp(suite, fetch_type, ffix, ppt_pad, &pt_decode_pad);
	ptu_run_fp(suite, fetch_type, ffix, ppt_psb, &pt_decode_psb);
	ptu_run_fp(suite, fetch_type, ffix, ppt_tip, &pt_decode_tip);
	ptu_run_fp(suite, fetch_type, ffix, ppt_tnt_64, &pt_decode_tnt_64);
	ptu_run_fp(suite, fetch_type, ffix, ppt_tip_pge, &pt_decode_tip_pge);
	ptu_run_fp(suite, fetch_type, ffix, ppt_tip_pgd, &pt_decode_tip_pgd);
	ptu_run_fp(suite, fetch_type, ffix, ppt_fup, &pt_decode_fup);
	ptu_run_fp(suite, fetch_type, ffix, ppt_pip, &pt_decode_pip);
	ptu_run_fp(suite, fetch_type, ffix, ppt_ovf, &pt_decode_ovf);
	ptu_run_fp(suite, fetch_type, ffix, ppt_psbend, &pt_decode_psbend);
	ptu_run_fp(suite, fetch_type, ffix, ppt_tsc, &pt_decode_tsc);
	ptu_run_fp(suite, fetch_type, ffix, ppt_cbr, &pt_decode_cbr);
	ptu_run_fp(suite, fetch_type, ffix, ppt_tma, &pt_decode_tma);
	ptu_run_fp(suite, fetch_type, ffix, ppt_mtc, &pt_decode_mtc);
	ptu_run_fp(suite, fetch_type, ffix, ppt_cyc, &pt_decode_cyc);
	ptu_run_fp(suite, fetch_type, ffix, ppt_stop, &pt_decode_stop);
	ptu_run_fp(suite, fetch_type, ffix, ppt_vmcs, &pt_decode_vmcs);
	ptu_run_fp(suite, fetch_type, ffix, ppt_mnt, &pt_decode_mnt);
	ptu_run_fp(suite, fetch_type, ffix, ppt_exstop, &pt_decode_exstop);
	ptu_run_fp(suite, fetch_type, ffix, ppt_mwait, &pt_decode_mwait);
	ptu_run_fp(suite, fetch_type, ffix, ppt_pwre, &pt_decode_pwre);
	ptu_run_fp(suite, fetch_type, ffix, ppt_pwrx, &pt_decode_pwrx);
	ptu_run_fp(suite, fetch_type, ffix, ppt_ptw, &pt_decode_ptw);

	ptu_run_f(suite, fetch_tnt_8, ffix);
	ptu_run_f(suite, fetch_mode_exec, ffix);
	ptu_run_f(suite, fetch_mode_tsx, ffix);
	ptu_run_f(suite, fetch_exstop_ip, ffix);

	return ptunit_report(&suite);
}


/* Dummy decode functions to satisfy link dependencies.
 *
 * As a nice side-effect, we will know if we need to add more tests when
 * adding new decoder functions.
 */
int pt_pkt_decode_unknown(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_unknown(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_pad(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_pad(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_psb(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_psb(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_tip(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_tip(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_tnt_8(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_tnt_8(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_tnt_64(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_tnt_64(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_tip_pge(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_tip_pge(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_tip_pgd(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_tip_pgd(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_fup(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

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

int pt_pkt_decode_pip(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

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

int pt_pkt_decode_ovf(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_ovf(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_mode(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

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

int pt_pkt_decode_psbend(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_psbend(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_tsc(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

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

int pt_pkt_decode_cbr(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

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

int pt_pkt_decode_tma(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_tma(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_mtc(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_mtc(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_cyc(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_cyc(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_stop(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_stop(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_vmcs(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

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

int pt_pkt_decode_mnt(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

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

int pt_pkt_decode_exstop(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_exstop(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_mwait(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_mwait(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_pwre(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_pwre(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_pwrx(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_pwrx(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}

int pt_pkt_decode_ptw(struct pt_packet_decoder *d, struct pt_packet *p)
{
	(void) d;
	(void) p;

	return -pte_internal;
}
int pt_qry_decode_ptw(struct pt_query_decoder *d)
{
	(void) d;

	return -pte_internal;
}
