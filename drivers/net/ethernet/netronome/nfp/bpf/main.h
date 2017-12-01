/*
 * Copyright (C) 2016-2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __NFP_BPF_H__
#define __NFP_BPF_H__ 1

#include <linux/bitfield.h>
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/list.h>
#include <linux/types.h>

#include "../nfp_asm.h"

/* For branch fixup logic use up-most byte of branch instruction as scratch
 * area.  Remember to clear this before sending instructions to HW!
 */
#define OP_BR_SPECIAL	0xff00000000000000ULL

enum br_special {
	OP_BR_NORMAL = 0,
	OP_BR_GO_OUT,
	OP_BR_GO_ABORT,
};

enum static_regs {
	STATIC_REG_IMM		= 21, /* Bank AB */
	STATIC_REG_STACK	= 22, /* Bank A */
	STATIC_REG_PKT_LEN	= 22, /* Bank B */
};

enum pkt_vec {
	PKT_VEC_PKT_LEN		= 0,
	PKT_VEC_PKT_PTR		= 2,
};

#define pv_len(np)	reg_lm(1, PKT_VEC_PKT_LEN)
#define pv_ctm_ptr(np)	reg_lm(1, PKT_VEC_PKT_PTR)

#define stack_reg(np)	reg_a(STATIC_REG_STACK)
#define stack_imm(np)	imm_b(np)
#define plen_reg(np)	reg_b(STATIC_REG_PKT_LEN)
#define pptr_reg(np)	pv_ctm_ptr(np)
#define imm_a(np)	reg_a(STATIC_REG_IMM)
#define imm_b(np)	reg_b(STATIC_REG_IMM)
#define imm_both(np)	reg_both(STATIC_REG_IMM)

#define NFP_BPF_ABI_FLAGS	reg_imm(0)
#define   NFP_BPF_ABI_FLAG_MARK	1

struct nfp_prog;
struct nfp_insn_meta;
typedef int (*instr_cb_t)(struct nfp_prog *, struct nfp_insn_meta *);

#define nfp_prog_first_meta(nfp_prog)					\
	list_first_entry(&(nfp_prog)->insns, struct nfp_insn_meta, l)
#define nfp_prog_last_meta(nfp_prog)					\
	list_last_entry(&(nfp_prog)->insns, struct nfp_insn_meta, l)
#define nfp_meta_next(meta)	list_next_entry(meta, l)
#define nfp_meta_prev(meta)	list_prev_entry(meta, l)

/**
 * struct nfp_insn_meta - BPF instruction wrapper
 * @insn: BPF instruction
 * @ptr: pointer type for memory operations
 * @ptr_not_const: pointer is not always constant
 * @jmp_dst: destination info for jump instructions
 * @off: index of first generated machine instruction (in nfp_prog.prog)
 * @n: eBPF instruction number
 * @skip: skip this instruction (optimized out)
 * @double_cb: callback for second part of the instruction
 * @l: link on nfp_prog->insns list
 */
struct nfp_insn_meta {
	struct bpf_insn insn;
	union {
		struct {
			struct bpf_reg_state ptr;
			bool ptr_not_const;
		};
		struct nfp_insn_meta *jmp_dst;
	};
	unsigned int off;
	unsigned short n;
	bool skip;
	instr_cb_t double_cb;

	struct list_head l;
};

#define BPF_SIZE_MASK	0x18

static inline u8 mbpf_class(const struct nfp_insn_meta *meta)
{
	return BPF_CLASS(meta->insn.code);
}

static inline u8 mbpf_src(const struct nfp_insn_meta *meta)
{
	return BPF_SRC(meta->insn.code);
}

static inline u8 mbpf_op(const struct nfp_insn_meta *meta)
{
	return BPF_OP(meta->insn.code);
}

static inline u8 mbpf_mode(const struct nfp_insn_meta *meta)
{
	return BPF_MODE(meta->insn.code);
}

/**
 * struct nfp_prog - nfp BPF program
 * @prog: machine code
 * @prog_len: number of valid instructions in @prog array
 * @__prog_alloc_len: alloc size of @prog array
 * @verifier_meta: temporary storage for verifier's insn meta
 * @type: BPF program type
 * @start_off: address of the first instruction in the memory
 * @last_bpf_off: address of the last instruction translated from BPF
 * @tgt_out: jump target for normal exit
 * @tgt_abort: jump target for abort (e.g. access outside of packet buffer)
 * @tgt_done: jump target to get the next packet
 * @n_translated: number of successfully translated instructions (for errors)
 * @error: error code if something went wrong
 * @stack_depth: max stack depth from the verifier
 * @insns: list of BPF instruction wrappers (struct nfp_insn_meta)
 */
struct nfp_prog {
	u64 *prog;
	unsigned int prog_len;
	unsigned int __prog_alloc_len;

	struct nfp_insn_meta *verifier_meta;

	enum bpf_prog_type type;

	unsigned int start_off;
	unsigned int last_bpf_off;
	unsigned int tgt_out;
	unsigned int tgt_abort;
	unsigned int tgt_done;

	unsigned int n_translated;
	int error;

	unsigned int stack_depth;

	struct list_head insns;
};

int nfp_bpf_jit(struct nfp_prog *prog);

extern const struct bpf_ext_analyzer_ops nfp_bpf_analyzer_ops;

struct netdev_bpf;
struct nfp_app;
struct nfp_net;

int nfp_net_bpf_offload(struct nfp_net *nn, struct bpf_prog *prog,
			bool old_prog);

int nfp_bpf_verifier_prep(struct nfp_app *app, struct nfp_net *nn,
			  struct netdev_bpf *bpf);
int nfp_bpf_translate(struct nfp_app *app, struct nfp_net *nn,
		      struct bpf_prog *prog);
int nfp_bpf_destroy(struct nfp_app *app, struct nfp_net *nn,
		    struct bpf_prog *prog);
struct nfp_insn_meta *
nfp_bpf_goto_meta(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
		  unsigned int insn_idx, unsigned int n_insns);
#endif
