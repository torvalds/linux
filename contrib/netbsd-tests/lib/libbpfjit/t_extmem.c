/*	$NetBSD: t_extmem.c,v 1.3 2014/07/14 19:11:15 alnsn Exp $ */

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
__RCSID("$NetBSD: t_extmem.c,v 1.3 2014/07/14 19:11:15 alnsn Exp $");

#include <atf-c.h>
#include <stdint.h>
#include <string.h>

#define __BPF_PRIVATE
#include <net/bpf.h>
#include <net/bpfjit.h>

static uint32_t retM(const bpf_ctx_t *bc, bpf_args_t *args, uint32_t A);

static const bpf_copfunc_t copfuncs[] = {
	&retM
};

static const bpf_ctx_t ctx = {
	.copfuncs = copfuncs,
	.nfuncs = sizeof(copfuncs) / sizeof(copfuncs[0]),
	.extwords = 4,
	.preinited = BPF_MEMWORD_INIT(0) | BPF_MEMWORD_INIT(3),
};

static uint32_t
retM(const bpf_ctx_t *bc, bpf_args_t *args, uint32_t A)
{

	return args->mem[(uintptr_t)args->arg];
}


ATF_TC(libbpfjit_extmem_load_default);
ATF_TC_HEAD(libbpfjit_extmem_load_default, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that external memory "
	    "is zero initialized by default");
}

ATF_TC_BODY(libbpfjit_extmem_load_default, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_MEM, 1),
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 0 };
	uint32_t mem[ctx.extwords];

	/* Pre-inited words. */
	mem[0] = 0;
	mem[3] = 3;

	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt),
		.mem = mem,
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	code = bpfjit_generate_code(&ctx, insns, insn_count);
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == 0);

	bpfjit_free_code(code);
}

ATF_TC(libbpfjit_extmem_load_preinited);
ATF_TC_HEAD(libbpfjit_extmem_load_preinited, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test a load of external "
	    "pre-initialized memory");
}

ATF_TC_BODY(libbpfjit_extmem_load_preinited, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_MEM, 3),
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 0 };
	uint32_t mem[ctx.extwords];

	/* Pre-inited words. */
	mem[0] = 0;
	mem[3] = 3;

	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt),
		.mem = mem,
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	code = bpfjit_generate_code(&ctx, insns, insn_count);
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == 3);

	bpfjit_free_code(code);
}

ATF_TC(libbpfjit_extmem_invalid_load);
ATF_TC_HEAD(libbpfjit_extmem_invalid_load, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that out-of-range load "
	    "fails validation");
}

ATF_TC_BODY(libbpfjit_extmem_invalid_load, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_MEM, 4),
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	ATF_CHECK(bpfjit_generate_code(&ctx, insns, insn_count) == NULL);
}

ATF_TC(libbpfjit_extmem_store);
ATF_TC_HEAD(libbpfjit_extmem_store, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test stores to external memory");
}

ATF_TC_BODY(libbpfjit_extmem_store, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_IMM, 1),        /* A <- 1     */
		BPF_STMT(BPF_LDX+BPF_W+BPF_IMM, 2), /* X <- 2     */
		BPF_STMT(BPF_ST, 1),                /* M[1] <- A  */
		BPF_STMT(BPF_ALU+BPF_ADD+BPF_X, 0), /* A <- A + X */
		BPF_STMT(BPF_STX, 2),               /* M[2] <- X  */
		BPF_STMT(BPF_ST, 3),                /* M[3] <- A  */
		BPF_STMT(BPF_RET+BPF_A, 0)          /* ret A      */
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 0 };
	uint32_t mem[ctx.extwords];

	/* Pre-inited words. */
	mem[0] = 0;
	mem[3] = 7;

	mem[1] = mem[2] = 0xdeadbeef;

	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt),
		.mem = mem,
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	code = bpfjit_generate_code(&ctx, insns, insn_count);
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == 3);

	bpfjit_free_code(code);

	ATF_CHECK(mem[0] == 0);
	ATF_CHECK(mem[1] == 1);
	ATF_CHECK(mem[2] == 2);
	ATF_CHECK(mem[3] == 3);
}

ATF_TC(libbpfjit_extmem_side_effect);
ATF_TC_HEAD(libbpfjit_extmem_side_effect, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that ABC optimization doesn\'t "
	    "skip stores to external memory");
}

ATF_TC_BODY(libbpfjit_extmem_side_effect, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_B+BPF_ABS, 0),  /* A <- P[0]  */
		BPF_STMT(BPF_LDX+BPF_W+BPF_IMM, 2), /* X <- 2     */
		BPF_STMT(BPF_ST, 1),                /* M[1] <- A  */
		BPF_STMT(BPF_ALU+BPF_ADD+BPF_X, 0), /* A <- A + X */
		BPF_STMT(BPF_STX, 2),               /* M[2] <- X  */
		BPF_STMT(BPF_ST, 3),                /* M[3] <- A  */
		BPF_STMT(BPF_LD+BPF_B+BPF_ABS, 99), /* A <- P[99] */
		BPF_STMT(BPF_RET+BPF_A, 0)          /* ret A      */
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 1 };
	uint32_t mem[ctx.extwords];

	/* Pre-inited words. */
	mem[0] = 0;
	mem[3] = 7;

	mem[1] = mem[2] = 0xdeadbeef;

	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt),
		.mem = mem,
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	code = bpfjit_generate_code(&ctx, insns, insn_count);
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == 0);

	bpfjit_free_code(code);

	ATF_CHECK(mem[0] == 0);
	ATF_CHECK(mem[1] == 1);
	ATF_CHECK(mem[2] == 2);
	ATF_CHECK(mem[3] == 3);
}

ATF_TC(libbpfjit_extmem_invalid_store);
ATF_TC_HEAD(libbpfjit_extmem_invalid_store, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that out-of-range store "
	    "fails validation");
}

ATF_TC_BODY(libbpfjit_extmem_invalid_store, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_ST, 4),
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	ATF_CHECK(bpfjit_generate_code(&ctx, insns, insn_count) == NULL);
}

ATF_TC(libbpfjit_cop_ret_mem);
ATF_TC_HEAD(libbpfjit_cop_ret_mem, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coprocessor function "
	    "that returns a content of external memory word");
}

ATF_TC_BODY(libbpfjit_cop_ret_mem, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_IMM, 13),
		BPF_STMT(BPF_ST, 2),
		BPF_STMT(BPF_LD+BPF_IMM, 137),
		BPF_STMT(BPF_ST, 1),
		BPF_STMT(BPF_MISC+BPF_COP, 0), // retM
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 0 };
	uint32_t mem[ctx.extwords];
	void *arg = (void*)(uintptr_t)2;

	/* Pre-inited words. */
	mem[0] = 0;
	mem[3] = 3;

	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt),
		.arg = arg,
		.mem = mem,
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	code = bpfjit_generate_code(&ctx, insns, insn_count);
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == 13);

	bpfjit_free_code(code);
}

ATF_TC(libbpfjit_cop_ret_preinited_mem);
ATF_TC_HEAD(libbpfjit_cop_ret_preinited_mem, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coprocessor function that "
	    "returns a content of external pre-initialized memory word");
}

ATF_TC_BODY(libbpfjit_cop_ret_preinited_mem, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_IMM, 13),
		BPF_STMT(BPF_ST, 2),
		BPF_STMT(BPF_LD+BPF_IMM, 137),
		BPF_STMT(BPF_ST, 1),
		BPF_STMT(BPF_MISC+BPF_COP, 0), // retM
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 0 };
	uint32_t mem[ctx.extwords];
	void *arg = (void*)(uintptr_t)3;

	/* Pre-inited words. */
	mem[0] = 0;
	mem[3] = 3;

	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt),
		.arg = arg,
		.mem = mem,
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	code = bpfjit_generate_code(&ctx, insns, insn_count);
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == 3);

	bpfjit_free_code(code);
}

ATF_TC(libbpfjit_copx_ret_mem);
ATF_TC_HEAD(libbpfjit_copx_ret_mem, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coprocessor function "
	    "that returns a content of external memory word");
}

ATF_TC_BODY(libbpfjit_copx_ret_mem, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_IMM, 13),
		BPF_STMT(BPF_ST, 2),
		BPF_STMT(BPF_LD+BPF_IMM, 137),
		BPF_STMT(BPF_ST, 1),
		BPF_STMT(BPF_LDX+BPF_IMM, 0), // retM
		BPF_STMT(BPF_MISC+BPF_COPX, 0),
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 0 };
	uint32_t mem[ctx.extwords];
	void *arg = (void*)(uintptr_t)2;

	/* Pre-inited words. */
	mem[0] = 0;
	mem[3] = 3;

	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt),
		.arg = arg,
		.mem = mem,
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	code = bpfjit_generate_code(&ctx, insns, insn_count);
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == 13);

	bpfjit_free_code(code);
}

ATF_TC(libbpfjit_copx_ret_preinited_mem);
ATF_TC_HEAD(libbpfjit_copx_ret_preinited_mem, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coprocessor function that "
	    "returns a content of external pre-initialized memory word");
}

ATF_TC_BODY(libbpfjit_copx_ret_preinited_mem, tc)
{
	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_IMM, 13),
		BPF_STMT(BPF_ST, 2),
		BPF_STMT(BPF_LD+BPF_IMM, 137),
		BPF_STMT(BPF_ST, 1),
		BPF_STMT(BPF_LDX+BPF_IMM, 0), // retM
		BPF_STMT(BPF_MISC+BPF_COPX, 0),
		BPF_STMT(BPF_RET+BPF_A, 0)
	};

	bpfjit_func_t code;
	uint8_t pkt[1] = { 0 };
	uint32_t mem[ctx.extwords];
	void *arg = (void*)(uintptr_t)3;

	/* Pre-inited words. */
	mem[0] = 0;
	mem[3] = 3;

	bpf_args_t args = {
		.pkt = pkt,
		.buflen = sizeof(pkt),
		.wirelen = sizeof(pkt),
		.arg = arg,
		.mem = mem,
	};

	size_t insn_count = sizeof(insns) / sizeof(insns[0]);

	code = bpfjit_generate_code(&ctx, insns, insn_count);
	ATF_REQUIRE(code != NULL);

	ATF_CHECK(code(&ctx, &args) == 3);

	bpfjit_free_code(code);
}

ATF_TP_ADD_TCS(tp)
{

	/*
	 * For every new test please also add a similar test
	 * to ../../net/bpfjit/t_extmem.c
	 */
	ATF_TP_ADD_TC(tp, libbpfjit_extmem_load_default);
	ATF_TP_ADD_TC(tp, libbpfjit_extmem_load_preinited);
	ATF_TP_ADD_TC(tp, libbpfjit_extmem_invalid_load);
	ATF_TP_ADD_TC(tp, libbpfjit_extmem_store);
	ATF_TP_ADD_TC(tp, libbpfjit_extmem_side_effect);
	ATF_TP_ADD_TC(tp, libbpfjit_extmem_invalid_store);
	ATF_TP_ADD_TC(tp, libbpfjit_cop_ret_mem);
	ATF_TP_ADD_TC(tp, libbpfjit_cop_ret_preinited_mem);
	ATF_TP_ADD_TC(tp, libbpfjit_copx_ret_mem);
	ATF_TP_ADD_TC(tp, libbpfjit_copx_ret_preinited_mem);

	return atf_no_error();
}
