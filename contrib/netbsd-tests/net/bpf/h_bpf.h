/*	$NetBSD: h_bpf.h,v 1.2 2014/07/08 21:44:26 alnsn Exp $	*/

/*-
 * Copyright (c) 2014 Alexander Nasonov.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _TESTS_NET_BPF_H_BPF_H_
#define _TESTS_NET_BPF_H_BPF_H_

#include <sys/param.h>
#include <sys/mbuf.h>

#include <net/bpf.h>
#include <net/bpfjit.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <stdint.h>
#include <string.h>

/* XXX These declarations don't look kosher. */
int rumpns_bpf_validate(const struct bpf_insn *, int);
unsigned int rumpns_bpf_filter_ext(const bpf_ctx_t *,
    const struct bpf_insn *, bpf_args_t *);
bpfjit_func_t rumpns_bpfjit_generate_code(const bpf_ctx_t *,
    const struct bpf_insn *, size_t);
void rumpns_bpfjit_free_code(bpfjit_func_t);

/*
 * Init mbuf chain with one or two chunks. The first chunk holds
 * [pkt, pkt + split] bytes, the second chunk (if it's not empty)
 * holds (pkt + split, pkt + pktsize) bytes.
 * The function returns (const uint8_t *)mb1.
 */
static inline const uint8_t *
init_mchain2(struct mbuf *mb1, struct mbuf *mb2,
    unsigned char pkt[], size_t pktsize, size_t split)
{

	(void)memset(mb1, 0, sizeof(*mb1));
	mb1->m_data = (char *)pkt;
	mb1->m_next = (split < pktsize) ? mb2 : NULL;
	mb1->m_len = (split < pktsize) ? split : pktsize;

	if (split < pktsize) {
		(void)memset(mb2, 0, sizeof(*mb2));
		mb2->m_next = NULL;
		mb2->m_data = (char *)&pkt[split];
		mb2->m_len = pktsize - split;
	}

	return (const uint8_t*)mb1;
}

/*
 * Compile and run a filter program.
 */
static inline unsigned int
exec_prog(struct bpf_insn *insns, size_t insn_count,
    unsigned char pkt[], size_t pktsize)
{
	bpfjit_func_t fn;
	bpf_args_t args;
	unsigned int res;

	args.pkt = (const uint8_t *)pkt;
	args.buflen = pktsize;
	args.wirelen = pktsize;

	rump_schedule();
	fn = rumpns_bpfjit_generate_code(NULL, insns, insn_count);
	rump_unschedule();

	res = fn(NULL, &args);

	rump_schedule();
	rumpns_bpfjit_free_code(fn);
	rump_unschedule();

	return res;
}

/*
 * Interpret a filter program with mbuf chain passed to bpf_filter_ext().
 */
static inline unsigned int
interp_prog_mchain2(struct bpf_insn *insns,
    unsigned char pkt[], size_t pktsize, size_t split)
{
	uint32_t mem[BPF_MEMWORDS];
	struct mbuf mb1, mb2;
	bpf_args_t args;
	unsigned int res;

	args.pkt = init_mchain2(&mb1, &mb2, pkt, pktsize, split);
	args.buflen = 0;
	args.wirelen = pktsize;
	args.mem = mem;

	rump_schedule();
	res = rumpns_bpf_filter_ext(NULL, insns, &args);
	rump_unschedule();

	return res;
}

/*
 * Compile and run a filter program with mbuf chain passed to compiled function.
 */
static inline unsigned int
exec_prog_mchain2(struct bpf_insn *insns, size_t insn_count,
    unsigned char pkt[], size_t pktsize, size_t split)
{
	bpfjit_func_t fn;
	struct mbuf mb1, mb2;
	bpf_args_t args;
	unsigned int res;

	args.pkt = init_mchain2(&mb1, &mb2, pkt, pktsize, split);
	args.buflen = 0;
	args.wirelen = pktsize;

	rump_schedule();
	fn = rumpns_bpfjit_generate_code(NULL, insns, insn_count);
	rump_unschedule();

	res = fn(NULL, &args);

	rump_schedule();
	rumpns_bpfjit_free_code(fn);
	rump_unschedule();

	return res;
}

static inline bool
prog_validate(struct bpf_insn *insns, size_t insn_count)
{
	bool res;

	rump_schedule();
	res = rumpns_bpf_validate(insns, insn_count);
	rump_unschedule();

	return res;
}

#endif /* _TESTS_NET_BPF_H_BPF_H_ */
