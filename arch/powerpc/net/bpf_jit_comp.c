/* bpf_jit_comp.c: BPF JIT compiler for PPC64
 *
 * Copyright 2011 Matt Evans <matt@ozlabs.org>, IBM Corporation
 *
 * Based on the x86 BPF compiler, by Eric Dumazet (eric.dumazet@gmail.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#include <linux/moduleloader.h>
#include <asm/cacheflush.h>
#include <linux/netdevice.h>
#include <linux/filter.h>
#include <linux/if_vlan.h>

#include "bpf_jit.h"

int bpf_jit_enable __read_mostly;

static inline void bpf_flush_icache(void *start, void *end)
{
	smp_wmb();
	flush_icache_range((unsigned long)start, (unsigned long)end);
}

static void bpf_jit_build_prologue(struct bpf_prog *fp, u32 *image,
				   struct codegen_context *ctx)
{
	int i;
	const struct sock_filter *filter = fp->insns;

	if (ctx->seen & (SEEN_MEM | SEEN_DATAREF)) {
		/* Make stackframe */
		if (ctx->seen & SEEN_DATAREF) {
			/* If we call any helpers (for loads), save LR */
			EMIT(PPC_INST_MFLR | __PPC_RT(R0));
			PPC_STD(0, 1, 16);

			/* Back up non-volatile regs. */
			PPC_STD(r_D, 1, -(8*(32-r_D)));
			PPC_STD(r_HL, 1, -(8*(32-r_HL)));
		}
		if (ctx->seen & SEEN_MEM) {
			/*
			 * Conditionally save regs r15-r31 as some will be used
			 * for M[] data.
			 */
			for (i = r_M; i < (r_M+16); i++) {
				if (ctx->seen & (1 << (i-r_M)))
					PPC_STD(i, 1, -(8*(32-i)));
			}
		}
		EMIT(PPC_INST_STDU | __PPC_RS(R1) | __PPC_RA(R1) |
		     (-BPF_PPC_STACKFRAME & 0xfffc));
	}

	if (ctx->seen & SEEN_DATAREF) {
		/*
		 * If this filter needs to access skb data,
		 * prepare r_D and r_HL:
		 *  r_HL = skb->len - skb->data_len
		 *  r_D	 = skb->data
		 */
		PPC_LWZ_OFFS(r_scratch1, r_skb, offsetof(struct sk_buff,
							 data_len));
		PPC_LWZ_OFFS(r_HL, r_skb, offsetof(struct sk_buff, len));
		PPC_SUB(r_HL, r_HL, r_scratch1);
		PPC_LD_OFFS(r_D, r_skb, offsetof(struct sk_buff, data));
	}

	if (ctx->seen & SEEN_XREG) {
		/*
		 * TODO: Could also detect whether first instr. sets X and
		 * avoid this (as below, with A).
		 */
		PPC_LI(r_X, 0);
	}

	switch (filter[0].code) {
	case BPF_RET | BPF_K:
	case BPF_LD | BPF_W | BPF_LEN:
	case BPF_LD | BPF_W | BPF_ABS:
	case BPF_LD | BPF_H | BPF_ABS:
	case BPF_LD | BPF_B | BPF_ABS:
		/* first instruction sets A register (or is RET 'constant') */
		break;
	default:
		/* make sure we dont leak kernel information to user */
		PPC_LI(r_A, 0);
	}
}

static void bpf_jit_build_epilogue(u32 *image, struct codegen_context *ctx)
{
	int i;

	if (ctx->seen & (SEEN_MEM | SEEN_DATAREF)) {
		PPC_ADDI(1, 1, BPF_PPC_STACKFRAME);
		if (ctx->seen & SEEN_DATAREF) {
			PPC_LD(0, 1, 16);
			PPC_MTLR(0);
			PPC_LD(r_D, 1, -(8*(32-r_D)));
			PPC_LD(r_HL, 1, -(8*(32-r_HL)));
		}
		if (ctx->seen & SEEN_MEM) {
			/* Restore any saved non-vol registers */
			for (i = r_M; i < (r_M+16); i++) {
				if (ctx->seen & (1 << (i-r_M)))
					PPC_LD(i, 1, -(8*(32-i)));
			}
		}
	}
	/* The RETs have left a return value in R3. */

	PPC_BLR();
}

#define CHOOSE_LOAD_FUNC(K, func) \
	((int)K < 0 ? ((int)K >= SKF_LL_OFF ? func##_negative_offset : func) : func##_positive_offset)

/* Assemble the body code between the prologue & epilogue. */
static int bpf_jit_build_body(struct bpf_prog *fp, u32 *image,
			      struct codegen_context *ctx,
			      unsigned int *addrs)
{
	const struct sock_filter *filter = fp->insns;
	int flen = fp->len;
	u8 *func;
	unsigned int true_cond;
	int i;

	/* Start of epilogue code */
	unsigned int exit_addr = addrs[flen];

	for (i = 0; i < flen; i++) {
		unsigned int K = filter[i].k;
		u16 code = bpf_anc_helper(&filter[i]);

		/*
		 * addrs[] maps a BPF bytecode address into a real offset from
		 * the start of the body code.
		 */
		addrs[i] = ctx->idx * 4;

		switch (code) {
			/*** ALU ops ***/
		case BPF_ALU | BPF_ADD | BPF_X: /* A += X; */
			ctx->seen |= SEEN_XREG;
			PPC_ADD(r_A, r_A, r_X);
			break;
		case BPF_ALU | BPF_ADD | BPF_K: /* A += K; */
			if (!K)
				break;
			PPC_ADDI(r_A, r_A, IMM_L(K));
			if (K >= 32768)
				PPC_ADDIS(r_A, r_A, IMM_HA(K));
			break;
		case BPF_ALU | BPF_SUB | BPF_X: /* A -= X; */
			ctx->seen |= SEEN_XREG;
			PPC_SUB(r_A, r_A, r_X);
			break;
		case BPF_ALU | BPF_SUB | BPF_K: /* A -= K */
			if (!K)
				break;
			PPC_ADDI(r_A, r_A, IMM_L(-K));
			if (K >= 32768)
				PPC_ADDIS(r_A, r_A, IMM_HA(-K));
			break;
		case BPF_ALU | BPF_MUL | BPF_X: /* A *= X; */
			ctx->seen |= SEEN_XREG;
			PPC_MUL(r_A, r_A, r_X);
			break;
		case BPF_ALU | BPF_MUL | BPF_K: /* A *= K */
			if (K < 32768)
				PPC_MULI(r_A, r_A, K);
			else {
				PPC_LI32(r_scratch1, K);
				PPC_MUL(r_A, r_A, r_scratch1);
			}
			break;
		case BPF_ALU | BPF_MOD | BPF_X: /* A %= X; */
			ctx->seen |= SEEN_XREG;
			PPC_CMPWI(r_X, 0);
			if (ctx->pc_ret0 != -1) {
				PPC_BCC(COND_EQ, addrs[ctx->pc_ret0]);
			} else {
				PPC_BCC_SHORT(COND_NE, (ctx->idx*4)+12);
				PPC_LI(r_ret, 0);
				PPC_JMP(exit_addr);
			}
			PPC_DIVWU(r_scratch1, r_A, r_X);
			PPC_MUL(r_scratch1, r_X, r_scratch1);
			PPC_SUB(r_A, r_A, r_scratch1);
			break;
		case BPF_ALU | BPF_MOD | BPF_K: /* A %= K; */
			PPC_LI32(r_scratch2, K);
			PPC_DIVWU(r_scratch1, r_A, r_scratch2);
			PPC_MUL(r_scratch1, r_scratch2, r_scratch1);
			PPC_SUB(r_A, r_A, r_scratch1);
			break;
		case BPF_ALU | BPF_DIV | BPF_X: /* A /= X; */
			ctx->seen |= SEEN_XREG;
			PPC_CMPWI(r_X, 0);
			if (ctx->pc_ret0 != -1) {
				PPC_BCC(COND_EQ, addrs[ctx->pc_ret0]);
			} else {
				/*
				 * Exit, returning 0; first pass hits here
				 * (longer worst-case code size).
				 */
				PPC_BCC_SHORT(COND_NE, (ctx->idx*4)+12);
				PPC_LI(r_ret, 0);
				PPC_JMP(exit_addr);
			}
			PPC_DIVWU(r_A, r_A, r_X);
			break;
		case BPF_ALU | BPF_DIV | BPF_K: /* A /= K */
			if (K == 1)
				break;
			PPC_LI32(r_scratch1, K);
			PPC_DIVWU(r_A, r_A, r_scratch1);
			break;
		case BPF_ALU | BPF_AND | BPF_X:
			ctx->seen |= SEEN_XREG;
			PPC_AND(r_A, r_A, r_X);
			break;
		case BPF_ALU | BPF_AND | BPF_K:
			if (!IMM_H(K))
				PPC_ANDI(r_A, r_A, K);
			else {
				PPC_LI32(r_scratch1, K);
				PPC_AND(r_A, r_A, r_scratch1);
			}
			break;
		case BPF_ALU | BPF_OR | BPF_X:
			ctx->seen |= SEEN_XREG;
			PPC_OR(r_A, r_A, r_X);
			break;
		case BPF_ALU | BPF_OR | BPF_K:
			if (IMM_L(K))
				PPC_ORI(r_A, r_A, IMM_L(K));
			if (K >= 65536)
				PPC_ORIS(r_A, r_A, IMM_H(K));
			break;
		case BPF_ANC | SKF_AD_ALU_XOR_X:
		case BPF_ALU | BPF_XOR | BPF_X: /* A ^= X */
			ctx->seen |= SEEN_XREG;
			PPC_XOR(r_A, r_A, r_X);
			break;
		case BPF_ALU | BPF_XOR | BPF_K: /* A ^= K */
			if (IMM_L(K))
				PPC_XORI(r_A, r_A, IMM_L(K));
			if (K >= 65536)
				PPC_XORIS(r_A, r_A, IMM_H(K));
			break;
		case BPF_ALU | BPF_LSH | BPF_X: /* A <<= X; */
			ctx->seen |= SEEN_XREG;
			PPC_SLW(r_A, r_A, r_X);
			break;
		case BPF_ALU | BPF_LSH | BPF_K:
			if (K == 0)
				break;
			else
				PPC_SLWI(r_A, r_A, K);
			break;
		case BPF_ALU | BPF_RSH | BPF_X: /* A >>= X; */
			ctx->seen |= SEEN_XREG;
			PPC_SRW(r_A, r_A, r_X);
			break;
		case BPF_ALU | BPF_RSH | BPF_K: /* A >>= K; */
			if (K == 0)
				break;
			else
				PPC_SRWI(r_A, r_A, K);
			break;
		case BPF_ALU | BPF_NEG:
			PPC_NEG(r_A, r_A);
			break;
		case BPF_RET | BPF_K:
			PPC_LI32(r_ret, K);
			if (!K) {
				if (ctx->pc_ret0 == -1)
					ctx->pc_ret0 = i;
			}
			/*
			 * If this isn't the very last instruction, branch to
			 * the epilogue if we've stuff to clean up.  Otherwise,
			 * if there's nothing to tidy, just return.  If we /are/
			 * the last instruction, we're about to fall through to
			 * the epilogue to return.
			 */
			if (i != flen - 1) {
				/*
				 * Note: 'seen' is properly valid only on pass
				 * #2.	Both parts of this conditional are the
				 * same instruction size though, meaning the
				 * first pass will still correctly determine the
				 * code size/addresses.
				 */
				if (ctx->seen)
					PPC_JMP(exit_addr);
				else
					PPC_BLR();
			}
			break;
		case BPF_RET | BPF_A:
			PPC_MR(r_ret, r_A);
			if (i != flen - 1) {
				if (ctx->seen)
					PPC_JMP(exit_addr);
				else
					PPC_BLR();
			}
			break;
		case BPF_MISC | BPF_TAX: /* X = A */
			PPC_MR(r_X, r_A);
			break;
		case BPF_MISC | BPF_TXA: /* A = X */
			ctx->seen |= SEEN_XREG;
			PPC_MR(r_A, r_X);
			break;

			/*** Constant loads/M[] access ***/
		case BPF_LD | BPF_IMM: /* A = K */
			PPC_LI32(r_A, K);
			break;
		case BPF_LDX | BPF_IMM: /* X = K */
			PPC_LI32(r_X, K);
			break;
		case BPF_LD | BPF_MEM: /* A = mem[K] */
			PPC_MR(r_A, r_M + (K & 0xf));
			ctx->seen |= SEEN_MEM | (1<<(K & 0xf));
			break;
		case BPF_LDX | BPF_MEM: /* X = mem[K] */
			PPC_MR(r_X, r_M + (K & 0xf));
			ctx->seen |= SEEN_MEM | (1<<(K & 0xf));
			break;
		case BPF_ST: /* mem[K] = A */
			PPC_MR(r_M + (K & 0xf), r_A);
			ctx->seen |= SEEN_MEM | (1<<(K & 0xf));
			break;
		case BPF_STX: /* mem[K] = X */
			PPC_MR(r_M + (K & 0xf), r_X);
			ctx->seen |= SEEN_XREG | SEEN_MEM | (1<<(K & 0xf));
			break;
		case BPF_LD | BPF_W | BPF_LEN: /*	A = skb->len; */
			BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, len) != 4);
			PPC_LWZ_OFFS(r_A, r_skb, offsetof(struct sk_buff, len));
			break;
		case BPF_LDX | BPF_W | BPF_LEN: /* X = skb->len; */
			PPC_LWZ_OFFS(r_X, r_skb, offsetof(struct sk_buff, len));
			break;

			/*** Ancillary info loads ***/
		case BPF_ANC | SKF_AD_PROTOCOL: /* A = ntohs(skb->protocol); */
			BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff,
						  protocol) != 2);
			PPC_NTOHS_OFFS(r_A, r_skb, offsetof(struct sk_buff,
							    protocol));
			break;
		case BPF_ANC | SKF_AD_IFINDEX:
			PPC_LD_OFFS(r_scratch1, r_skb, offsetof(struct sk_buff,
								dev));
			PPC_CMPDI(r_scratch1, 0);
			if (ctx->pc_ret0 != -1) {
				PPC_BCC(COND_EQ, addrs[ctx->pc_ret0]);
			} else {
				/* Exit, returning 0; first pass hits here. */
				PPC_BCC_SHORT(COND_NE, (ctx->idx*4)+12);
				PPC_LI(r_ret, 0);
				PPC_JMP(exit_addr);
			}
			BUILD_BUG_ON(FIELD_SIZEOF(struct net_device,
						  ifindex) != 4);
			PPC_LWZ_OFFS(r_A, r_scratch1,
				     offsetof(struct net_device, ifindex));
			break;
		case BPF_ANC | SKF_AD_MARK:
			BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, mark) != 4);
			PPC_LWZ_OFFS(r_A, r_skb, offsetof(struct sk_buff,
							  mark));
			break;
		case BPF_ANC | SKF_AD_RXHASH:
			BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, hash) != 4);
			PPC_LWZ_OFFS(r_A, r_skb, offsetof(struct sk_buff,
							  hash));
			break;
		case BPF_ANC | SKF_AD_VLAN_TAG:
		case BPF_ANC | SKF_AD_VLAN_TAG_PRESENT:
			BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, vlan_tci) != 2);
			BUILD_BUG_ON(VLAN_TAG_PRESENT != 0x1000);

			PPC_LHZ_OFFS(r_A, r_skb, offsetof(struct sk_buff,
							  vlan_tci));
			if (code == (BPF_ANC | SKF_AD_VLAN_TAG)) {
				PPC_ANDI(r_A, r_A, ~VLAN_TAG_PRESENT);
			} else {
				PPC_ANDI(r_A, r_A, VLAN_TAG_PRESENT);
				PPC_SRWI(r_A, r_A, 12);
			}
			break;
		case BPF_ANC | SKF_AD_QUEUE:
			BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff,
						  queue_mapping) != 2);
			PPC_LHZ_OFFS(r_A, r_skb, offsetof(struct sk_buff,
							  queue_mapping));
			break;
		case BPF_ANC | SKF_AD_CPU:
#ifdef CONFIG_SMP
			/*
			 * PACA ptr is r13:
			 * raw_smp_processor_id() = local_paca->paca_index
			 */
			BUILD_BUG_ON(FIELD_SIZEOF(struct paca_struct,
						  paca_index) != 2);
			PPC_LHZ_OFFS(r_A, 13,
				     offsetof(struct paca_struct, paca_index));
#else
			PPC_LI(r_A, 0);
#endif
			break;

			/*** Absolute loads from packet header/data ***/
		case BPF_LD | BPF_W | BPF_ABS:
			func = CHOOSE_LOAD_FUNC(K, sk_load_word);
			goto common_load;
		case BPF_LD | BPF_H | BPF_ABS:
			func = CHOOSE_LOAD_FUNC(K, sk_load_half);
			goto common_load;
		case BPF_LD | BPF_B | BPF_ABS:
			func = CHOOSE_LOAD_FUNC(K, sk_load_byte);
		common_load:
			/* Load from [K]. */
			ctx->seen |= SEEN_DATAREF;
			PPC_LI64(r_scratch1, func);
			PPC_MTLR(r_scratch1);
			PPC_LI32(r_addr, K);
			PPC_BLRL();
			/*
			 * Helper returns 'lt' condition on error, and an
			 * appropriate return value in r3
			 */
			PPC_BCC(COND_LT, exit_addr);
			break;

			/*** Indirect loads from packet header/data ***/
		case BPF_LD | BPF_W | BPF_IND:
			func = sk_load_word;
			goto common_load_ind;
		case BPF_LD | BPF_H | BPF_IND:
			func = sk_load_half;
			goto common_load_ind;
		case BPF_LD | BPF_B | BPF_IND:
			func = sk_load_byte;
		common_load_ind:
			/*
			 * Load from [X + K].  Negative offsets are tested for
			 * in the helper functions.
			 */
			ctx->seen |= SEEN_DATAREF | SEEN_XREG;
			PPC_LI64(r_scratch1, func);
			PPC_MTLR(r_scratch1);
			PPC_ADDI(r_addr, r_X, IMM_L(K));
			if (K >= 32768)
				PPC_ADDIS(r_addr, r_addr, IMM_HA(K));
			PPC_BLRL();
			/* If error, cr0.LT set */
			PPC_BCC(COND_LT, exit_addr);
			break;

		case BPF_LDX | BPF_B | BPF_MSH:
			func = CHOOSE_LOAD_FUNC(K, sk_load_byte_msh);
			goto common_load;
			break;

			/*** Jump and branches ***/
		case BPF_JMP | BPF_JA:
			if (K != 0)
				PPC_JMP(addrs[i + 1 + K]);
			break;

		case BPF_JMP | BPF_JGT | BPF_K:
		case BPF_JMP | BPF_JGT | BPF_X:
			true_cond = COND_GT;
			goto cond_branch;
		case BPF_JMP | BPF_JGE | BPF_K:
		case BPF_JMP | BPF_JGE | BPF_X:
			true_cond = COND_GE;
			goto cond_branch;
		case BPF_JMP | BPF_JEQ | BPF_K:
		case BPF_JMP | BPF_JEQ | BPF_X:
			true_cond = COND_EQ;
			goto cond_branch;
		case BPF_JMP | BPF_JSET | BPF_K:
		case BPF_JMP | BPF_JSET | BPF_X:
			true_cond = COND_NE;
			/* Fall through */
		cond_branch:
			/* same targets, can avoid doing the test :) */
			if (filter[i].jt == filter[i].jf) {
				if (filter[i].jt > 0)
					PPC_JMP(addrs[i + 1 + filter[i].jt]);
				break;
			}

			switch (code) {
			case BPF_JMP | BPF_JGT | BPF_X:
			case BPF_JMP | BPF_JGE | BPF_X:
			case BPF_JMP | BPF_JEQ | BPF_X:
				ctx->seen |= SEEN_XREG;
				PPC_CMPLW(r_A, r_X);
				break;
			case BPF_JMP | BPF_JSET | BPF_X:
				ctx->seen |= SEEN_XREG;
				PPC_AND_DOT(r_scratch1, r_A, r_X);
				break;
			case BPF_JMP | BPF_JEQ | BPF_K:
			case BPF_JMP | BPF_JGT | BPF_K:
			case BPF_JMP | BPF_JGE | BPF_K:
				if (K < 32768)
					PPC_CMPLWI(r_A, K);
				else {
					PPC_LI32(r_scratch1, K);
					PPC_CMPLW(r_A, r_scratch1);
				}
				break;
			case BPF_JMP | BPF_JSET | BPF_K:
				if (K < 32768)
					/* PPC_ANDI is /only/ dot-form */
					PPC_ANDI(r_scratch1, r_A, K);
				else {
					PPC_LI32(r_scratch1, K);
					PPC_AND_DOT(r_scratch1, r_A,
						    r_scratch1);
				}
				break;
			}
			/* Sometimes branches are constructed "backward", with
			 * the false path being the branch and true path being
			 * a fallthrough to the next instruction.
			 */
			if (filter[i].jt == 0)
				/* Swap the sense of the branch */
				PPC_BCC(true_cond ^ COND_CMP_TRUE,
					addrs[i + 1 + filter[i].jf]);
			else {
				PPC_BCC(true_cond, addrs[i + 1 + filter[i].jt]);
				if (filter[i].jf != 0)
					PPC_JMP(addrs[i + 1 + filter[i].jf]);
			}
			break;
		default:
			/* The filter contains something cruel & unusual.
			 * We don't handle it, but also there shouldn't be
			 * anything missing from our list.
			 */
			if (printk_ratelimit())
				pr_err("BPF filter opcode %04x (@%d) unsupported\n",
				       filter[i].code, i);
			return -ENOTSUPP;
		}

	}
	/* Set end-of-body-code address for exit. */
	addrs[i] = ctx->idx * 4;

	return 0;
}

void bpf_jit_compile(struct bpf_prog *fp)
{
	unsigned int proglen;
	unsigned int alloclen;
	u32 *image = NULL;
	u32 *code_base;
	unsigned int *addrs;
	struct codegen_context cgctx;
	int pass;
	int flen = fp->len;

	if (!bpf_jit_enable)
		return;

	addrs = kzalloc((flen+1) * sizeof(*addrs), GFP_KERNEL);
	if (addrs == NULL)
		return;

	/*
	 * There are multiple assembly passes as the generated code will change
	 * size as it settles down, figuring out the max branch offsets/exit
	 * paths required.
	 *
	 * The range of standard conditional branches is +/- 32Kbytes.	Since
	 * BPF_MAXINSNS = 4096, we can only jump from (worst case) start to
	 * finish with 8 bytes/instruction.  Not feasible, so long jumps are
	 * used, distinct from short branches.
	 *
	 * Current:
	 *
	 * For now, both branch types assemble to 2 words (short branches padded
	 * with a NOP); this is less efficient, but assembly will always complete
	 * after exactly 3 passes:
	 *
	 * First pass: No code buffer; Program is "faux-generated" -- no code
	 * emitted but maximum size of output determined (and addrs[] filled
	 * in).	 Also, we note whether we use M[], whether we use skb data, etc.
	 * All generation choices assumed to be 'worst-case', e.g. branches all
	 * far (2 instructions), return path code reduction not available, etc.
	 *
	 * Second pass: Code buffer allocated with size determined previously.
	 * Prologue generated to support features we have seen used.  Exit paths
	 * determined and addrs[] is filled in again, as code may be slightly
	 * smaller as a result.
	 *
	 * Third pass: Code generated 'for real', and branch destinations
	 * determined from now-accurate addrs[] map.
	 *
	 * Ideal:
	 *
	 * If we optimise this, near branches will be shorter.	On the
	 * first assembly pass, we should err on the side of caution and
	 * generate the biggest code.  On subsequent passes, branches will be
	 * generated short or long and code size will reduce.  With smaller
	 * code, more branches may fall into the short category, and code will
	 * reduce more.
	 *
	 * Finally, if we see one pass generate code the same size as the
	 * previous pass we have converged and should now generate code for
	 * real.  Allocating at the end will also save the memory that would
	 * otherwise be wasted by the (small) current code shrinkage.
	 * Preferably, we should do a small number of passes (e.g. 5) and if we
	 * haven't converged by then, get impatient and force code to generate
	 * as-is, even if the odd branch would be left long.  The chances of a
	 * long jump are tiny with all but the most enormous of BPF filter
	 * inputs, so we should usually converge on the third pass.
	 */

	cgctx.idx = 0;
	cgctx.seen = 0;
	cgctx.pc_ret0 = -1;
	/* Scouting faux-generate pass 0 */
	if (bpf_jit_build_body(fp, 0, &cgctx, addrs))
		/* We hit something illegal or unsupported. */
		goto out;

	/*
	 * Pretend to build prologue, given the features we've seen.  This will
	 * update ctgtx.idx as it pretends to output instructions, then we can
	 * calculate total size from idx.
	 */
	bpf_jit_build_prologue(fp, 0, &cgctx);
	bpf_jit_build_epilogue(0, &cgctx);

	proglen = cgctx.idx * 4;
	alloclen = proglen + FUNCTION_DESCR_SIZE;
	image = module_alloc(alloclen);
	if (!image)
		goto out;

	code_base = image + (FUNCTION_DESCR_SIZE/4);

	/* Code generation passes 1-2 */
	for (pass = 1; pass < 3; pass++) {
		/* Now build the prologue, body code & epilogue for real. */
		cgctx.idx = 0;
		bpf_jit_build_prologue(fp, code_base, &cgctx);
		bpf_jit_build_body(fp, code_base, &cgctx, addrs);
		bpf_jit_build_epilogue(code_base, &cgctx);

		if (bpf_jit_enable > 1)
			pr_info("Pass %d: shrink = %d, seen = 0x%x\n", pass,
				proglen - (cgctx.idx * 4), cgctx.seen);
	}

	if (bpf_jit_enable > 1)
		/* Note that we output the base address of the code_base
		 * rather than image, since opcodes are in code_base.
		 */
		bpf_jit_dump(flen, proglen, pass, code_base);

	if (image) {
		bpf_flush_icache(code_base, code_base + (proglen/4));
		/* Function descriptor nastiness: Address + TOC */
		((u64 *)image)[0] = (u64)code_base;
		((u64 *)image)[1] = local_paca->kernel_toc;
		fp->bpf_func = (void *)image;
		fp->jited = 1;
	}
out:
	kfree(addrs);
	return;
}

void bpf_jit_free(struct bpf_prog *fp)
{
	if (fp->jited)
		module_free(NULL, fp->bpf_func);
	kfree(fp);
}
