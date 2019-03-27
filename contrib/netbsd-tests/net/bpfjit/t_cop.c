/*	$NetBSD: t_cop.c,v 1.4 2017/01/13 21:30:42 christos Exp $ */

/*-
 * Copyright (c) 2014 Alexander Nasonov.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: t_cop.c,v 1.4 2017/01/13 21:30:42 christos Exp $");

#include <stdint.h>
#include <string.h>

#define __BPF_PRIVATE
#include <net/bpf.h>
#include <net/bpfjit.h>

#include "../../net/bpf/h_bpf.h"

/* XXX: atf-c.h has collisions with mbuf */
#undef m_type
#undef m_data
#include <atf-c.h>

#include "h_macros.h"

static uint32_t retA(const bpf_ctx_t *bc, bpf_args_t *args, uint32_t A);
static uint32_t retBL(const bpf_ctx_t *bc, bpf_args_t *args, uint32_t A);
static uint32_t retWL(const bpf_ctx_t *bc, bpf_args_t *args, uint32_t A);
static uint32_t retNF(const bpf_ctx_t *bc, bpf_args_t *args, uint32_t A);
static uint32_t setARG(const bpf_ctx_t *bc, bpf_args_t *args, uint32_t A);

static const bpf_copfunc_t copfuncs[] = {
	&retA,
	&retBL,
	&retWL,
	&retNF,
	&setARG
};

static const bpf_ctx_t ctx = {
	.copfuncs = copfuncs,
	.nfuncs = sizeof(copfuncs) / sizeof(copfuncs[0]),
	.extwords = 0
};

static uint32_t
retA(const bpf_ctx_t *bc, bpf_args_t *args, uint32_t A)
{

	return A;
}

static uint32_t
retBL(const bpf_ctx_t *bc, bpf_args_t *args, uint32_t A)
{

	return args->buflen;
}

static uint32_t
retWL(const bpf_ctx_t *bc, bpf_args_t *args, uint32_t A)
{

	return args->wirelen;
}

static uint32_t
retNF(const bpf_ctx_t *bc, bpf_args_t *args, uint32_t A)
{

	return bc->nfuncs;
}

/*
 * COP function with a side effect.
 */
static uint32_t
setARG(const bpf_ctx_t *bc, bpf_args_t *args, uint32_t A)
{
	bool *arg = (bool *)args->arg;
	bool old = *arg;

	*arg = true;
	return old;
}

ATF_TC(bpfjit_cop_no_ctx);
ATF_TC_HEAD(bpfjit_cop_no_ctx, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that bpf program with BPF_COP "
	    "instruction isn't valid without a context");
}

ATF_TC_BODY(bpfjit_cop_no_ctx, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_MISC+BPF_COP, 0),
		BPF_STMT(BPF_RET+BPF_K, 7)
	};

	bpfjit_func_t code;

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	RZ(rump_init());

	ATF_CHECK(!prog_validate(insns, insn_count));

	rump_schedule();
	code = rumpns_bpfjit_generate_code(NULL, insns, insn_count);
	rump_unschedule();
	ATF_CHECK(code == NULL);
}

ATF_TC(bpfjit_cop_ret_A);
ATF_TC_HEAD(bpfjit_cop_ret_A, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coprocessor function "
	    "that returns a content of the A register");
}

ATF_TC_BODY(bpfjit_cop_ret_A, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_IMM, 13),
		BPF_STMT(BPF_MISC+BPF_COP, 0), // retA
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 0 };
	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt),
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	RZ(rump_init());

	rump_schedule();
	code = rumpns_bpfjit_generate_code(&ctx, insns, insn_count);
	rump_unschedule();
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == 13);

	rump_schedule();
	rumpns_bpfjit_free_code(code);
	rump_unschedule();
}

ATF_TC(bpfjit_cop_ret_buflen);
ATF_TC_HEAD(bpfjit_cop_ret_buflen, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coprocessor function "
	    "that returns the buflen argument");
}

ATF_TC_BODY(bpfjit_cop_ret_buflen, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_IMM, 13),
		BPF_STMT(BPF_MISC+BPF_COP, 1), // retBL
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 0 };
	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt)
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	RZ(rump_init());

	rump_schedule();
	code = rumpns_bpfjit_generate_code(&ctx, insns, insn_count);
	rump_unschedule();
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == sizeof(pkt));

	rump_schedule();
	rumpns_bpfjit_free_code(code);
	rump_unschedule();
}

ATF_TC(bpfjit_cop_ret_wirelen);
ATF_TC_HEAD(bpfjit_cop_ret_wirelen, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coprocessor function "
	    "that returns the wirelen argument");
}

ATF_TC_BODY(bpfjit_cop_ret_wirelen, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_IMM, 13),
		BPF_STMT(BPF_MISC+BPF_COP, 2), // retWL
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 0 };
	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt)
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	RZ(rump_init());

	rump_schedule();
	code = rumpns_bpfjit_generate_code(&ctx, insns, insn_count);
	rump_unschedule();
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == sizeof(pkt));

	rump_schedule();
	rumpns_bpfjit_free_code(code);
	rump_unschedule();
}

ATF_TC(bpfjit_cop_ret_nfuncs);
ATF_TC_HEAD(bpfjit_cop_ret_nfuncs, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coprocessor function "
	    "that returns nfuncs member of the context argument");
}

ATF_TC_BODY(bpfjit_cop_ret_nfuncs, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_IMM, 13),
		BPF_STMT(BPF_MISC+BPF_COP, 3), // retNF
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 0 };
	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt)
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	RZ(rump_init());

	rump_schedule();
	code = rumpns_bpfjit_generate_code(&ctx, insns, insn_count);
	rump_unschedule();
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == ctx.nfuncs);

	rump_schedule();
	rumpns_bpfjit_free_code(code);
	rump_unschedule();
}

ATF_TC(bpfjit_cop_side_effect);
ATF_TC_HEAD(bpfjit_cop_side_effect, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that ABC optimization doesn't skip BPF_COP call");
}

ATF_TC_BODY(bpfjit_cop_side_effect, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_IMM, 13),
		BPF_STMT(BPF_LD+BPF_B+BPF_ABS, 0),
		BPF_STMT(BPF_MISC+BPF_COP, 4), // setARG
		BPF_STMT(BPF_LD+BPF_B+BPF_ABS, 99999),
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	bool arg = false;
	uint8_t pkt[1] = { 0 };
	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt),
		.mem = NULL,
		.arg = &arg
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	RZ(rump_init());

	rump_schedule();
	code = rumpns_bpfjit_generate_code(&ctx, insns, insn_count);
	rump_unschedule();
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == 0);
	ATF_CHECK(arg == true);

	rump_schedule();
	rumpns_bpfjit_free_code(code);
	rump_unschedule();
}

ATF_TC(bpfjit_cop_copx);
ATF_TC_HEAD(bpfjit_cop_copx, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test BPF_COP call followed by BPF_COPX call");
}

ATF_TC_BODY(bpfjit_cop_copx, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_IMM, 1),         /* A <- 1    */
		BPF_STMT(BPF_MISC+BPF_COP, 0),       /* retA      */
		BPF_STMT(BPF_MISC+BPF_TAX, 0),       /* X <- A    */
		BPF_STMT(BPF_LD+BPF_B+BPF_ABS, 0),   /* A = P[0]  */
		BPF_STMT(BPF_ALU+BPF_ADD+BPF_X, 1),  /* A = A + X */
		BPF_STMT(BPF_MISC+BPF_TAX, 0),       /* X <- A    */
		BPF_STMT(BPF_MISC+BPF_COPX, 0),      /* retNF     */
		BPF_STMT(BPF_ALU+BPF_ADD+BPF_X, 1),  /* A = A + X */
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 2 };
	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt),
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	RZ(rump_init());

	rump_schedule();
	code = rumpns_bpfjit_generate_code(&ctx, insns, insn_count);
	rump_unschedule();
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == 3 + ctx.nfuncs);

	rump_schedule();
	rumpns_bpfjit_free_code(code);
	rump_unschedule();
}

ATF_TC(bpfjit_cop_invalid_index);
ATF_TC_HEAD(bpfjit_cop_invalid_index, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that out-of-range coprocessor function fails validation");
}

ATF_TC_BODY(bpfjit_cop_invalid_index, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_IMM, 13),
		BPF_STMT(BPF_MISC+BPF_COP, 6), // invalid index
		BPF_STMT(BPF_RET+BPF_K, 27)
	};

	bpfjit_func_t code;
	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	RZ(rump_init());

	rump_schedule();
	code = rumpns_bpfjit_generate_code(&ctx, insns, insn_count);
	rump_unschedule();
	ATF_CHECK(code == NULL);
}

ATF_TC(bpfjit_copx_no_ctx);
ATF_TC_HEAD(bpfjit_copx_no_ctx, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that bpf program with BPF_COPX "
	    "instruction isn't valid without a context");
}

ATF_TC_BODY(bpfjit_copx_no_ctx, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_MISC+BPF_COP, 0),
		BPF_STMT(BPF_RET+BPF_K, 7)
	};

	bpfjit_func_t code;

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	RZ(rump_init());

	ATF_CHECK(!prog_validate(insns, insn_count));

	rump_schedule();
	code = rumpns_bpfjit_generate_code(NULL, insns, insn_count);
	rump_unschedule();
	ATF_CHECK(code == NULL);
}

ATF_TC(bpfjit_copx_ret_A);
ATF_TC_HEAD(bpfjit_copx_ret_A, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coprocessor function "
	    "that returns a content of the A register");
}

ATF_TC_BODY(bpfjit_copx_ret_A, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_IMM, 13),
		BPF_STMT(BPF_LDX+BPF_IMM, 0), // retA
		BPF_STMT(BPF_MISC+BPF_COPX, 0),
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 0 };
	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt),
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	RZ(rump_init());

	rump_schedule();
	code = rumpns_bpfjit_generate_code(&ctx, insns, insn_count);
	rump_unschedule();
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == 13);

	rump_schedule();
	rumpns_bpfjit_free_code(code);
	rump_unschedule();
}

ATF_TC(bpfjit_copx_ret_buflen);
ATF_TC_HEAD(bpfjit_copx_ret_buflen, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coprocessor function "
	    "that returns the buflen argument");
}

ATF_TC_BODY(bpfjit_copx_ret_buflen, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_IMM, 13),
		BPF_STMT(BPF_LDX+BPF_IMM, 1), // retBL
		BPF_STMT(BPF_MISC+BPF_COPX, 0),
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 0 };
	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt)
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	RZ(rump_init());

	rump_schedule();
	code = rumpns_bpfjit_generate_code(&ctx, insns, insn_count);
	rump_unschedule();
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == sizeof(pkt));

	rump_schedule();
	rumpns_bpfjit_free_code(code);
	rump_unschedule();
}

ATF_TC(bpfjit_copx_ret_wirelen);
ATF_TC_HEAD(bpfjit_copx_ret_wirelen, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coprocessor function "
	    "that returns the wirelen argument");
}

ATF_TC_BODY(bpfjit_copx_ret_wirelen, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LDX+BPF_IMM, 2), // retWL
		BPF_STMT(BPF_LD+BPF_IMM, 13),
		BPF_STMT(BPF_MISC+BPF_COPX, 0),
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 0 };
	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt)
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	RZ(rump_init());

	rump_schedule();
	code = rumpns_bpfjit_generate_code(&ctx, insns, insn_count);
	rump_unschedule();
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == sizeof(pkt));

	rump_schedule();
	rumpns_bpfjit_free_code(code);
	rump_unschedule();
}

ATF_TC(bpfjit_copx_ret_nfuncs);
ATF_TC_HEAD(bpfjit_copx_ret_nfuncs, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coprocessor function "
	    "that returns nfuncs member of the context argument");
}

ATF_TC_BODY(bpfjit_copx_ret_nfuncs, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_IMM, 13),
		BPF_STMT(BPF_LDX+BPF_IMM, 3), // retNF
		BPF_STMT(BPF_MISC+BPF_COPX, 0),
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 0 };
	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt)
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	RZ(rump_init());

	rump_schedule();
	code = rumpns_bpfjit_generate_code(&ctx, insns, insn_count);
	rump_unschedule();
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == ctx.nfuncs);

	rump_schedule();
	rumpns_bpfjit_free_code(code);
	rump_unschedule();
}

ATF_TC(bpfjit_copx_side_effect);
ATF_TC_HEAD(bpfjit_copx_side_effect, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that ABC optimization doesn't skip BPF_COPX call");
}

ATF_TC_BODY(bpfjit_copx_side_effect, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_IMM, 13),
		BPF_STMT(BPF_LD+BPF_B+BPF_ABS, 0),
		BPF_STMT(BPF_LDX+BPF_IMM, 4), // setARG
		BPF_STMT(BPF_MISC+BPF_COPX, 0),
		BPF_STMT(BPF_LD+BPF_B+BPF_ABS, 99999),
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	bool arg = false;
	uint8_t pkt[1] = { 0 };
	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt),
		.mem = NULL,
		.arg = &arg
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	RZ(rump_init());

	rump_schedule();
	code = rumpns_bpfjit_generate_code(&ctx, insns, insn_count);
	rump_unschedule();
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == 0);
	ATF_CHECK(arg == true);

	rump_schedule();
	rumpns_bpfjit_free_code(code);
	rump_unschedule();
}

ATF_TC(bpfjit_copx_cop);
ATF_TC_HEAD(bpfjit_copx_cop, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test BPF_COPX call followed by BPF_COP call");
}

ATF_TC_BODY(bpfjit_copx_cop, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LDX+BPF_IMM, 2),        /* X <- 2    */
		BPF_STMT(BPF_MISC+BPF_COPX, 0),      /* retWL     */
		BPF_STMT(BPF_ALU+BPF_ADD+BPF_X, 1),  /* A = A + X */
		BPF_STMT(BPF_MISC+BPF_TAX, 0),       /* X <- A    */
		BPF_STMT(BPF_LD+BPF_B+BPF_ABS, 0),   /* A = P[0]  */
		BPF_STMT(BPF_ALU+BPF_ADD+BPF_X, 1),  /* A = A + X */
		BPF_STMT(BPF_MISC+BPF_TAX, 0),       /* X <- A    */
		BPF_STMT(BPF_MISC+BPF_COP, 3),      /* retNF     */
		BPF_STMT(BPF_ALU+BPF_ADD+BPF_X, 1),  /* A = A + X */
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 2 };
	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt),
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	RZ(rump_init());

	rump_schedule();
	code = rumpns_bpfjit_generate_code(&ctx, insns, insn_count);
	rump_unschedule();
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == 5 + ctx.nfuncs);

	rump_schedule();
	rumpns_bpfjit_free_code(code);
	rump_unschedule();
}

ATF_TC(bpfjit_copx_invalid_index);
ATF_TC_HEAD(bpfjit_copx_invalid_index, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that out-of-range BPF_COPX call fails at runtime");
}

ATF_TC_BODY(bpfjit_copx_invalid_index, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LDX+BPF_IMM, 5), // invalid index
		BPF_STMT(BPF_MISC+BPF_COPX, 0),
		BPF_STMT(BPF_RET+BPF_K, 27)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 0 };
	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt)
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	RZ(rump_init());

	rump_schedule();
	code = rumpns_bpfjit_generate_code(&ctx, insns, insn_count);
	rump_unschedule();
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == 0);

	rump_schedule();
	rumpns_bpfjit_free_code(code);
	rump_unschedule();
}

ATF_TP_ADD_TCS(tp)
{

	/*
	 * For every new test please also add a similar test
	 * to ../../lib/libbpfjit/t_cop.c
	 */
	ATF_TP_ADD_TC(tp, bpfjit_cop_no_ctx);
	ATF_TP_ADD_TC(tp, bpfjit_cop_ret_A);
	ATF_TP_ADD_TC(tp, bpfjit_cop_ret_buflen);
	ATF_TP_ADD_TC(tp, bpfjit_cop_ret_wirelen);
	ATF_TP_ADD_TC(tp, bpfjit_cop_ret_nfuncs);
	ATF_TP_ADD_TC(tp, bpfjit_cop_side_effect);
	ATF_TP_ADD_TC(tp, bpfjit_cop_copx);
	ATF_TP_ADD_TC(tp, bpfjit_cop_invalid_index);

	ATF_TP_ADD_TC(tp, bpfjit_copx_no_ctx);
	ATF_TP_ADD_TC(tp, bpfjit_copx_ret_A);
	ATF_TP_ADD_TC(tp, bpfjit_copx_ret_buflen);
	ATF_TP_ADD_TC(tp, bpfjit_copx_ret_wirelen);
	ATF_TP_ADD_TC(tp, bpfjit_copx_ret_nfuncs);
	ATF_TP_ADD_TC(tp, bpfjit_copx_side_effect);
	ATF_TP_ADD_TC(tp, bpfjit_copx_cop);
	ATF_TP_ADD_TC(tp, bpfjit_copx_invalid_index);

	return atf_no_error();
}
