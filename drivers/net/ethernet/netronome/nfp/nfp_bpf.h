/*
 * Copyright (C) 2016 Netronome Systems, Inc.
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
#include <linux/list.h>
#include <linux/types.h>

#define FIELD_FIT(mask, val)  (!((((u64)val) << __bf_shf(mask)) & ~(mask)))

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
	STATIC_REG_PKT		= 1,
#define REG_PKT_BANK	ALU_DST_A
	STATIC_REG_IMM		= 2, /* Bank AB */
};

enum nfp_bpf_action_type {
	NN_ACT_TC_DROP,
	NN_ACT_TC_REDIR,
	NN_ACT_DIRECT,
};

/* Software register representation, hardware encoding in asm.h */
#define NN_REG_TYPE	GENMASK(31, 24)
#define NN_REG_VAL	GENMASK(7, 0)

enum nfp_bpf_reg_type {
	NN_REG_GPR_A =	BIT(0),
	NN_REG_GPR_B =	BIT(1),
	NN_REG_NNR =	BIT(2),
	NN_REG_XFER =	BIT(3),
	NN_REG_IMM =	BIT(4),
	NN_REG_NONE =	BIT(5),
};

#define NN_REG_GPR_BOTH	(NN_REG_GPR_A | NN_REG_GPR_B)

#define reg_both(x)	((x) | FIELD_PREP(NN_REG_TYPE, NN_REG_GPR_BOTH))
#define reg_a(x)	((x) | FIELD_PREP(NN_REG_TYPE, NN_REG_GPR_A))
#define reg_b(x)	((x) | FIELD_PREP(NN_REG_TYPE, NN_REG_GPR_B))
#define reg_nnr(x)	((x) | FIELD_PREP(NN_REG_TYPE, NN_REG_NNR))
#define reg_xfer(x)	((x) | FIELD_PREP(NN_REG_TYPE, NN_REG_XFER))
#define reg_imm(x)	((x) | FIELD_PREP(NN_REG_TYPE, NN_REG_IMM))
#define reg_none()	(FIELD_PREP(NN_REG_TYPE, NN_REG_NONE))

#define pkt_reg(np)	reg_a((np)->regs_per_thread - STATIC_REG_PKT)
#define imm_a(np)	reg_a((np)->regs_per_thread - STATIC_REG_IMM)
#define imm_b(np)	reg_b((np)->regs_per_thread - STATIC_REG_IMM)
#define imm_both(np)	reg_both((np)->regs_per_thread - STATIC_REG_IMM)

#define NFP_BPF_ABI_FLAGS	reg_nnr(0)
#define   NFP_BPF_ABI_FLAG_MARK	1
#define NFP_BPF_ABI_MARK	reg_nnr(1)
#define NFP_BPF_ABI_PKT		reg_nnr(2)
#define NFP_BPF_ABI_LEN		reg_nnr(3)

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
 * @off: index of first generated machine instruction (in nfp_prog.prog)
 * @n: eBPF instruction number
 * @skip: skip this instruction (optimized out)
 * @double_cb: callback for second part of the instruction
 * @l: link on nfp_prog->insns list
 */
struct nfp_insn_meta {
	struct bpf_insn insn;
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
 * @act: BPF program/action type (TC DA, TC with action, XDP etc.)
 * @num_regs: number of registers used by this program
 * @regs_per_thread: number of basic registers allocated per thread
 * @start_off: address of the first instruction in the memory
 * @tgt_out: jump target for normal exit
 * @tgt_abort: jump target for abort (e.g. access outside of packet buffer)
 * @tgt_done: jump target to get the next packet
 * @n_translated: number of successfully translated instructions (for errors)
 * @error: error code if something went wrong
 * @insns: list of BPF instruction wrappers (struct nfp_insn_meta)
 */
struct nfp_prog {
	u64 *prog;
	unsigned int prog_len;
	unsigned int __prog_alloc_len;

	enum nfp_bpf_action_type act;

	unsigned int num_regs;
	unsigned int regs_per_thread;

	unsigned int start_off;
	unsigned int tgt_out;
	unsigned int tgt_abort;
	unsigned int tgt_done;

	unsigned int n_translated;
	int error;

	struct list_head insns;
};

struct nfp_bpf_result {
	unsigned int n_instr;
	bool dense_mode;
};

#ifdef CONFIG_BPF_SYSCALL
int
nfp_bpf_jit(struct bpf_prog *filter, void *prog, enum nfp_bpf_action_type act,
	    unsigned int prog_start, unsigned int prog_done,
	    unsigned int prog_sz, struct nfp_bpf_result *res);
#else
int
nfp_bpf_jit(struct bpf_prog *filter, void *prog, enum nfp_bpf_action_type act,
	    unsigned int prog_start, unsigned int prog_done,
	    unsigned int prog_sz, struct nfp_bpf_result *res)
{
	return -ENOTSUPP;
}
#endif

int nfp_prog_verify(struct nfp_prog *nfp_prog, struct bpf_prog *prog);

#endif
