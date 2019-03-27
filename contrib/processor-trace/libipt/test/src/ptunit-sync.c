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

#include "pt_sync.h"
#include "pt_opcodes.h"

#include "intel-pt.h"


/* A test fixture for sync tests. */
struct sync_fixture {
	/* The trace buffer. */
	uint8_t buffer[1024];

	/* A trace configuration. */
	struct pt_config config;

	/* The test fixture initialization and finalization functions. */
	struct ptunit_result (*init)(struct sync_fixture *);
	struct ptunit_result (*fini)(struct sync_fixture *);
};

static struct ptunit_result sfix_init(struct sync_fixture *sfix)
{
	memset(sfix->buffer, 0xcd, sizeof(sfix->buffer));

	memset(&sfix->config, 0, sizeof(sfix->config));
	sfix->config.size = sizeof(sfix->config);
	sfix->config.begin = sfix->buffer;
	sfix->config.end = sfix->buffer + sizeof(sfix->buffer);

	return ptu_passed();
}

static void sfix_encode_psb(uint8_t *pos)
{
	int i;

	*pos++ = pt_opc_psb;
	*pos++ = pt_ext_psb;

	for (i = 0; i < pt_psb_repeat_count; ++i) {
		*pos++ = pt_psb_hi;
		*pos++ = pt_psb_lo;
	}
}


static struct ptunit_result sync_fwd_null(struct sync_fixture *sfix)
{
	const uint8_t *sync;
	int errcode;

	errcode = pt_sync_forward(NULL, sfix->config.begin, &sfix->config);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_sync_forward(&sync, NULL, &sfix->config);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_sync_forward(&sync, sfix->config.begin, NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result sync_bwd_null(struct sync_fixture *sfix)
{
	const uint8_t *sync;
	int errcode;

	errcode = pt_sync_backward(NULL, sfix->config.begin, &sfix->config);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_sync_backward(&sync, NULL, &sfix->config);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_sync_backward(&sync, sfix->config.begin, NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result sync_fwd_empty(struct sync_fixture *sfix)
{
	const uint8_t *sync;
	int errcode;

	sfix->config.end = sfix->config.begin;

	errcode = pt_sync_forward(&sync, sfix->config.begin, &sfix->config);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result sync_bwd_empty(struct sync_fixture *sfix)
{
	const uint8_t *sync;
	int errcode;

	sfix->config.end = sfix->config.begin;

	errcode = pt_sync_backward(&sync, sfix->config.end, &sfix->config);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result sync_fwd_none(struct sync_fixture *sfix)
{
	const uint8_t *sync;
	int errcode;

	errcode = pt_sync_forward(&sync, sfix->config.begin, &sfix->config);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result sync_bwd_none(struct sync_fixture *sfix)
{
	const uint8_t *sync;
	int errcode;

	errcode = pt_sync_backward(&sync, sfix->config.end, &sfix->config);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result sync_fwd_here(struct sync_fixture *sfix)
{
	const uint8_t *sync;
	int errcode;

	sfix_encode_psb(sfix->config.begin);

	errcode = pt_sync_forward(&sync, sfix->config.begin, &sfix->config);
	ptu_int_eq(errcode, 0);
	ptu_ptr_eq(sync, sfix->config.begin);

	return ptu_passed();
}

static struct ptunit_result sync_bwd_here(struct sync_fixture *sfix)
{
	const uint8_t *sync;
	int errcode;

	sfix_encode_psb(sfix->config.end - ptps_psb);

	errcode = pt_sync_backward(&sync, sfix->config.end, &sfix->config);
	ptu_int_eq(errcode, 0);
	ptu_ptr_eq(sync, sfix->config.end - ptps_psb);

	return ptu_passed();
}

static struct ptunit_result sync_fwd(struct sync_fixture *sfix)
{
	const uint8_t *sync;
	int errcode;

	sfix_encode_psb(sfix->config.begin + 0x23);

	errcode = pt_sync_forward(&sync, sfix->config.begin, &sfix->config);
	ptu_int_eq(errcode, 0);
	ptu_ptr_eq(sync, sfix->config.begin + 0x23);

	return ptu_passed();
}

static struct ptunit_result sync_bwd(struct sync_fixture *sfix)
{
	const uint8_t *sync;
	int errcode;

	sfix_encode_psb(sfix->config.begin + 0x23);

	errcode = pt_sync_backward(&sync, sfix->config.end, &sfix->config);
	ptu_int_eq(errcode, 0);
	ptu_ptr_eq(sync, sfix->config.begin + 0x23);

	return ptu_passed();
}

static struct ptunit_result sync_fwd_past(struct sync_fixture *sfix)
{
	const uint8_t *sync;
	int errcode;

	sfix_encode_psb(sfix->config.begin);

	errcode = pt_sync_forward(&sync, sfix->config.begin + ptps_psb,
				  &sfix->config);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result sync_bwd_past(struct sync_fixture *sfix)
{
	const uint8_t *sync;
	int errcode;

	sfix_encode_psb(sfix->config.end - ptps_psb);

	errcode = pt_sync_backward(&sync, sfix->config.end - ptps_psb,
				   &sfix->config);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result sync_fwd_cutoff(struct sync_fixture *sfix)
{
	const uint8_t *sync;
	int errcode;

	sfix_encode_psb(sfix->config.begin);
	sfix_encode_psb(sfix->config.end - ptps_psb);
	sfix->config.begin += 1;
	sfix->config.end -= 1;

	errcode = pt_sync_forward(&sync, sfix->config.begin, &sfix->config);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

static struct ptunit_result sync_bwd_cutoff(struct sync_fixture *sfix)
{
	const uint8_t *sync;
	int errcode;

	sfix_encode_psb(sfix->config.begin);
	sfix_encode_psb(sfix->config.end - ptps_psb);
	sfix->config.begin += 1;
	sfix->config.end -= 1;

	errcode = pt_sync_backward(&sync, sfix->config.end, &sfix->config);
	ptu_int_eq(errcode, -pte_eos);

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct sync_fixture sfix;
	struct ptunit_suite suite;

	sfix.init = sfix_init;
	sfix.fini = NULL;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run_f(suite, sync_fwd_null, sfix);
	ptu_run_f(suite, sync_bwd_null, sfix);

	ptu_run_f(suite, sync_fwd_empty, sfix);
	ptu_run_f(suite, sync_bwd_empty, sfix);

	ptu_run_f(suite, sync_fwd_none, sfix);
	ptu_run_f(suite, sync_bwd_none, sfix);

	ptu_run_f(suite, sync_fwd_here, sfix);
	ptu_run_f(suite, sync_bwd_here, sfix);

	ptu_run_f(suite, sync_fwd, sfix);
	ptu_run_f(suite, sync_bwd, sfix);

	ptu_run_f(suite, sync_fwd_past, sfix);
	ptu_run_f(suite, sync_bwd_past, sfix);

	ptu_run_f(suite, sync_fwd_cutoff, sfix);
	ptu_run_f(suite, sync_bwd_cutoff, sfix);

	return ptunit_report(&suite);
}
