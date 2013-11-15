/*
 * BPF Jit compiler for s390.
 *
 * Copyright IBM Corp. 2012
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */
#include <linux/moduleloader.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/filter.h>
#include <linux/random.h>
#include <linux/init.h>
#include <asm/cacheflush.h>
#include <asm/processor.h>
#include <asm/facility.h>

/*
 * Conventions:
 *   %r2 = skb pointer
 *   %r3 = offset parameter
 *   %r4 = scratch register / length parameter
 *   %r5 = BPF A accumulator
 *   %r8 = return address
 *   %r9 = save register for skb pointer
 *   %r10 = skb->data
 *   %r11 = skb->len - skb->data_len (headlen)
 *   %r12 = BPF X accumulator
 *   %r13 = literal pool pointer
 *   0(%r15) - 63(%r15) scratch memory array with BPF_MEMWORDS
 */
int bpf_jit_enable __read_mostly;

/*
 * assembly code in arch/x86/net/bpf_jit.S
 */
extern u8 sk_load_word[], sk_load_half[], sk_load_byte[], sk_load_byte_msh[];
extern u8 sk_load_word_ind[], sk_load_half_ind[], sk_load_byte_ind[];

struct bpf_jit {
	unsigned int seen;
	u8 *start;
	u8 *prg;
	u8 *mid;
	u8 *lit;
	u8 *end;
	u8 *base_ip;
	u8 *ret0_ip;
	u8 *exit_ip;
	unsigned int off_load_word;
	unsigned int off_load_half;
	unsigned int off_load_byte;
	unsigned int off_load_bmsh;
	unsigned int off_load_iword;
	unsigned int off_load_ihalf;
	unsigned int off_load_ibyte;
};

#define BPF_SIZE_MAX	4096	/* Max size for program */

#define SEEN_DATAREF	1	/* might call external helpers */
#define SEEN_XREG	2	/* ebx is used */
#define SEEN_MEM	4	/* use mem[] for temporary storage */
#define SEEN_RET0	8	/* pc_ret0 points to a valid return 0 */
#define SEEN_LITERAL	16	/* code uses literals */
#define SEEN_LOAD_WORD	32	/* code uses sk_load_word */
#define SEEN_LOAD_HALF	64	/* code uses sk_load_half */
#define SEEN_LOAD_BYTE	128	/* code uses sk_load_byte */
#define SEEN_LOAD_BMSH	256	/* code uses sk_load_byte_msh */
#define SEEN_LOAD_IWORD	512	/* code uses sk_load_word_ind */
#define SEEN_LOAD_IHALF	1024	/* code uses sk_load_half_ind */
#define SEEN_LOAD_IBYTE	2048	/* code uses sk_load_byte_ind */

#define EMIT2(op)					\
({							\
	if (jit->prg + 2 <= jit->mid)			\
		*(u16 *) jit->prg = op;			\
	jit->prg += 2;					\
})

#define EMIT4(op)					\
({							\
	if (jit->prg + 4 <= jit->mid)			\
		*(u32 *) jit->prg = op;			\
	jit->prg += 4;					\
})

#define EMIT4_DISP(op, disp)				\
({							\
	unsigned int __disp = (disp) & 0xfff;		\
	EMIT4(op | __disp);				\
})

#define EMIT4_IMM(op, imm)				\
({							\
	unsigned int __imm = (imm) & 0xffff;		\
	EMIT4(op | __imm);				\
})

#define EMIT4_PCREL(op, pcrel)				\
({							\
	long __pcrel = ((pcrel) >> 1) & 0xffff;		\
	EMIT4(op | __pcrel);				\
})

#define EMIT6(op1, op2)					\
({							\
	if (jit->prg + 6 <= jit->mid) {			\
		*(u32 *) jit->prg = op1;		\
		*(u16 *) (jit->prg + 4) = op2;		\
	}						\
	jit->prg += 6;					\
})

#define EMIT6_DISP(op1, op2, disp)			\
({							\
	unsigned int __disp = (disp) & 0xfff;		\
	EMIT6(op1 | __disp, op2);			\
})

#define EMIT6_IMM(op, imm)				\
({							\
	unsigned int __imm = (imm);			\
	EMIT6(op | (__imm >> 16), __imm & 0xffff);	\
})

#define EMIT_CONST(val)					\
({							\
	unsigned int ret;				\
	ret = (unsigned int) (jit->lit - jit->base_ip);	\
	jit->seen |= SEEN_LITERAL;			\
	if (jit->lit + 4 <= jit->end)			\
		*(u32 *) jit->lit = val;		\
	jit->lit += 4;					\
	ret;						\
})

#define EMIT_FN_CONST(bit, fn)				\
({							\
	unsigned int ret;				\
	ret = (unsigned int) (jit->lit - jit->base_ip);	\
	if (jit->seen & bit) {				\
		jit->seen |= SEEN_LITERAL;		\
		if (jit->lit + 8 <= jit->end)		\
			*(void **) jit->lit = fn;	\
		jit->lit += 8;				\
	}						\
	ret;						\
})

static void bpf_jit_prologue(struct bpf_jit *jit)
{
	/* Save registers and create stack frame if necessary */
	if (jit->seen & SEEN_DATAREF) {
		/* stmg %r8,%r15,88(%r15) */
		EMIT6(0xeb8ff058, 0x0024);
		/* lgr %r14,%r15 */
		EMIT4(0xb90400ef);
		/* ahi %r15,<offset> */
		EMIT4_IMM(0xa7fa0000, (jit->seen & SEEN_MEM) ? -112 : -80);
		/* stg %r14,152(%r15) */
		EMIT6(0xe3e0f098, 0x0024);
	} else if ((jit->seen & SEEN_XREG) && (jit->seen & SEEN_LITERAL))
		/* stmg %r12,%r13,120(%r15) */
		EMIT6(0xebcdf078, 0x0024);
	else if (jit->seen & SEEN_XREG)
		/* stg %r12,120(%r15) */
		EMIT6(0xe3c0f078, 0x0024);
	else if (jit->seen & SEEN_LITERAL)
		/* stg %r13,128(%r15) */
		EMIT6(0xe3d0f080, 0x0024);

	/* Setup literal pool */
	if (jit->seen & SEEN_LITERAL) {
		/* basr %r13,0 */
		EMIT2(0x0dd0);
		jit->base_ip = jit->prg;
	}
	jit->off_load_word = EMIT_FN_CONST(SEEN_LOAD_WORD, sk_load_word);
	jit->off_load_half = EMIT_FN_CONST(SEEN_LOAD_HALF, sk_load_half);
	jit->off_load_byte = EMIT_FN_CONST(SEEN_LOAD_BYTE, sk_load_byte);
	jit->off_load_bmsh = EMIT_FN_CONST(SEEN_LOAD_BMSH, sk_load_byte_msh);
	jit->off_load_iword = EMIT_FN_CONST(SEEN_LOAD_IWORD, sk_load_word_ind);
	jit->off_load_ihalf = EMIT_FN_CONST(SEEN_LOAD_IHALF, sk_load_half_ind);
	jit->off_load_ibyte = EMIT_FN_CONST(SEEN_LOAD_IBYTE, sk_load_byte_ind);

	/* Filter needs to access skb data */
	if (jit->seen & SEEN_DATAREF) {
		/* l %r11,<len>(%r2) */
		EMIT4_DISP(0x58b02000, offsetof(struct sk_buff, len));
		/* s %r11,<data_len>(%r2) */
		EMIT4_DISP(0x5bb02000, offsetof(struct sk_buff, data_len));
		/* lg %r10,<data>(%r2) */
		EMIT6_DISP(0xe3a02000, 0x0004,
			   offsetof(struct sk_buff, data));
	}
}

static void bpf_jit_epilogue(struct bpf_jit *jit)
{
	/* Return 0 */
	if (jit->seen & SEEN_RET0) {
		jit->ret0_ip = jit->prg;
		/* lghi %r2,0 */
		EMIT4(0xa7290000);
	}
	jit->exit_ip = jit->prg;
	/* Restore registers */
	if (jit->seen & SEEN_DATAREF)
		/* lmg %r8,%r15,<offset>(%r15) */
		EMIT6_DISP(0xeb8ff000, 0x0004,
			   (jit->seen & SEEN_MEM) ? 200 : 168);
	else if ((jit->seen & SEEN_XREG) && (jit->seen & SEEN_LITERAL))
		/* lmg %r12,%r13,120(%r15) */
		EMIT6(0xebcdf078, 0x0004);
	else if (jit->seen & SEEN_XREG)
		/* lg %r12,120(%r15) */
		EMIT6(0xe3c0f078, 0x0004);
	else if (jit->seen & SEEN_LITERAL)
		/* lg %r13,128(%r15) */
		EMIT6(0xe3d0f080, 0x0004);
	/* br %r14 */
	EMIT2(0x07fe);
}

/* Helper to find the offset of pkt_type in sk_buff
 * Make sure its still a 3bit field starting at the MSBs within a byte.
 */
#define PKT_TYPE_MAX 0xe0
static int pkt_type_offset;

static int __init bpf_pkt_type_offset_init(void)
{
	struct sk_buff skb_probe = {
		.pkt_type = ~0,
	};
	char *ct = (char *)&skb_probe;
	int off;

	pkt_type_offset = -1;
	for (off = 0; off < sizeof(struct sk_buff); off++) {
		if (!ct[off])
			continue;
		if (ct[off] == PKT_TYPE_MAX)
			pkt_type_offset = off;
		else {
			/* Found non matching bit pattern, fix needed. */
			WARN_ON_ONCE(1);
			pkt_type_offset = -1;
			return -1;
		}
	}
	return 0;
}
device_initcall(bpf_pkt_type_offset_init);

/*
 * make sure we dont leak kernel information to user
 */
static void bpf_jit_noleaks(struct bpf_jit *jit, struct sock_filter *filter)
{
	/* Clear temporary memory if (seen & SEEN_MEM) */
	if (jit->seen & SEEN_MEM)
		/* xc 0(64,%r15),0(%r15) */
		EMIT6(0xd73ff000, 0xf000);
	/* Clear X if (seen & SEEN_XREG) */
	if (jit->seen & SEEN_XREG)
		/* lhi %r12,0 */
		EMIT4(0xa7c80000);
	/* Clear A if the first register does not set it. */
	switch (filter[0].code) {
	case BPF_S_LD_W_ABS:
	case BPF_S_LD_H_ABS:
	case BPF_S_LD_B_ABS:
	case BPF_S_LD_W_LEN:
	case BPF_S_LD_W_IND:
	case BPF_S_LD_H_IND:
	case BPF_S_LD_B_IND:
	case BPF_S_LDX_B_MSH:
	case BPF_S_LD_IMM:
	case BPF_S_LD_MEM:
	case BPF_S_MISC_TXA:
	case BPF_S_ANC_PROTOCOL:
	case BPF_S_ANC_PKTTYPE:
	case BPF_S_ANC_IFINDEX:
	case BPF_S_ANC_MARK:
	case BPF_S_ANC_QUEUE:
	case BPF_S_ANC_HATYPE:
	case BPF_S_ANC_RXHASH:
	case BPF_S_ANC_CPU:
	case BPF_S_ANC_VLAN_TAG:
	case BPF_S_ANC_VLAN_TAG_PRESENT:
	case BPF_S_RET_K:
		/* first instruction sets A register */
		break;
	default: /* A = 0 */
		/* lhi %r5,0 */
		EMIT4(0xa7580000);
	}
}

static int bpf_jit_insn(struct bpf_jit *jit, struct sock_filter *filter,
			unsigned int *addrs, int i, int last)
{
	unsigned int K;
	int offset;
	unsigned int mask;

	K = filter->k;
	switch (filter->code) {
	case BPF_S_ALU_ADD_X: /* A += X */
		jit->seen |= SEEN_XREG;
		/* ar %r5,%r12 */
		EMIT2(0x1a5c);
		break;
	case BPF_S_ALU_ADD_K: /* A += K */
		if (!K)
			break;
		if (K <= 16383)
			/* ahi %r5,<K> */
			EMIT4_IMM(0xa75a0000, K);
		else if (test_facility(21))
			/* alfi %r5,<K> */
			EMIT6_IMM(0xc25b0000, K);
		else
			/* a %r5,<d(K)>(%r13) */
			EMIT4_DISP(0x5a50d000, EMIT_CONST(K));
		break;
	case BPF_S_ALU_SUB_X: /* A -= X */
		jit->seen |= SEEN_XREG;
		/* sr %r5,%r12 */
		EMIT2(0x1b5c);
		break;
	case BPF_S_ALU_SUB_K: /* A -= K */
		if (!K)
			break;
		if (K <= 16384)
			/* ahi %r5,-K */
			EMIT4_IMM(0xa75a0000, -K);
		else if (test_facility(21))
			/* alfi %r5,-K */
			EMIT6_IMM(0xc25b0000, -K);
		else
			/* s %r5,<d(K)>(%r13) */
			EMIT4_DISP(0x5b50d000, EMIT_CONST(K));
		break;
	case BPF_S_ALU_MUL_X: /* A *= X */
		jit->seen |= SEEN_XREG;
		/* msr %r5,%r12 */
		EMIT4(0xb252005c);
		break;
	case BPF_S_ALU_MUL_K: /* A *= K */
		if (K <= 16383)
			/* mhi %r5,K */
			EMIT4_IMM(0xa75c0000, K);
		else if (test_facility(34))
			/* msfi %r5,<K> */
			EMIT6_IMM(0xc2510000, K);
		else
			/* ms %r5,<d(K)>(%r13) */
			EMIT4_DISP(0x7150d000, EMIT_CONST(K));
		break;
	case BPF_S_ALU_DIV_X: /* A /= X */
		jit->seen |= SEEN_XREG | SEEN_RET0;
		/* ltr %r12,%r12 */
		EMIT2(0x12cc);
		/* jz <ret0> */
		EMIT4_PCREL(0xa7840000, (jit->ret0_ip - jit->prg));
		/* lhi %r4,0 */
		EMIT4(0xa7480000);
		/* dr %r4,%r12 */
		EMIT2(0x1d4c);
		break;
	case BPF_S_ALU_DIV_K: /* A = reciprocal_divide(A, K) */
		/* m %r4,<d(K)>(%r13) */
		EMIT4_DISP(0x5c40d000, EMIT_CONST(K));
		/* lr %r5,%r4 */
		EMIT2(0x1854);
		break;
	case BPF_S_ALU_MOD_X: /* A %= X */
		jit->seen |= SEEN_XREG | SEEN_RET0;
		/* ltr %r12,%r12 */
		EMIT2(0x12cc);
		/* jz <ret0> */
		EMIT4_PCREL(0xa7840000, (jit->ret0_ip - jit->prg));
		/* lhi %r4,0 */
		EMIT4(0xa7480000);
		/* dr %r4,%r12 */
		EMIT2(0x1d4c);
		/* lr %r5,%r4 */
		EMIT2(0x1854);
		break;
	case BPF_S_ALU_MOD_K: /* A %= K */
		/* lhi %r4,0 */
		EMIT4(0xa7480000);
		/* d %r4,<d(K)>(%r13) */
		EMIT4_DISP(0x5d40d000, EMIT_CONST(K));
		/* lr %r5,%r4 */
		EMIT2(0x1854);
		break;
	case BPF_S_ALU_AND_X: /* A &= X */
		jit->seen |= SEEN_XREG;
		/* nr %r5,%r12 */
		EMIT2(0x145c);
		break;
	case BPF_S_ALU_AND_K: /* A &= K */
		if (test_facility(21))
			/* nilf %r5,<K> */
			EMIT6_IMM(0xc05b0000, K);
		else
			/* n %r5,<d(K)>(%r13) */
			EMIT4_DISP(0x5450d000, EMIT_CONST(K));
		break;
	case BPF_S_ALU_OR_X: /* A |= X */
		jit->seen |= SEEN_XREG;
		/* or %r5,%r12 */
		EMIT2(0x165c);
		break;
	case BPF_S_ALU_OR_K: /* A |= K */
		if (test_facility(21))
			/* oilf %r5,<K> */
			EMIT6_IMM(0xc05d0000, K);
		else
			/* o %r5,<d(K)>(%r13) */
			EMIT4_DISP(0x5650d000, EMIT_CONST(K));
		break;
	case BPF_S_ANC_ALU_XOR_X: /* A ^= X; */
	case BPF_S_ALU_XOR_X:
		jit->seen |= SEEN_XREG;
		/* xr %r5,%r12 */
		EMIT2(0x175c);
		break;
	case BPF_S_ALU_XOR_K: /* A ^= K */
		if (!K)
			break;
		/* x %r5,<d(K)>(%r13) */
		EMIT4_DISP(0x5750d000, EMIT_CONST(K));
		break;
	case BPF_S_ALU_LSH_X: /* A <<= X; */
		jit->seen |= SEEN_XREG;
		/* sll %r5,0(%r12) */
		EMIT4(0x8950c000);
		break;
	case BPF_S_ALU_LSH_K: /* A <<= K */
		if (K == 0)
			break;
		/* sll %r5,K */
		EMIT4_DISP(0x89500000, K);
		break;
	case BPF_S_ALU_RSH_X: /* A >>= X; */
		jit->seen |= SEEN_XREG;
		/* srl %r5,0(%r12) */
		EMIT4(0x8850c000);
		break;
	case BPF_S_ALU_RSH_K: /* A >>= K; */
		if (K == 0)
			break;
		/* srl %r5,K */
		EMIT4_DISP(0x88500000, K);
		break;
	case BPF_S_ALU_NEG: /* A = -A */
		/* lnr %r5,%r5 */
		EMIT2(0x1155);
		break;
	case BPF_S_JMP_JA: /* ip += K */
		offset = addrs[i + K] + jit->start - jit->prg;
		EMIT4_PCREL(0xa7f40000, offset);
		break;
	case BPF_S_JMP_JGT_K: /* ip += (A > K) ? jt : jf */
		mask = 0x200000; /* jh */
		goto kbranch;
	case BPF_S_JMP_JGE_K: /* ip += (A >= K) ? jt : jf */
		mask = 0xa00000; /* jhe */
		goto kbranch;
	case BPF_S_JMP_JEQ_K: /* ip += (A == K) ? jt : jf */
		mask = 0x800000; /* je */
kbranch:	/* Emit compare if the branch targets are different */
		if (filter->jt != filter->jf) {
			if (K <= 16383)
				/* chi %r5,<K> */
				EMIT4_IMM(0xa75e0000, K);
			else if (test_facility(21))
				/* clfi %r5,<K> */
				EMIT6_IMM(0xc25f0000, K);
			else
				/* c %r5,<d(K)>(%r13) */
				EMIT4_DISP(0x5950d000, EMIT_CONST(K));
		}
branch:		if (filter->jt == filter->jf) {
			if (filter->jt == 0)
				break;
			/* j <jt> */
			offset = addrs[i + filter->jt] + jit->start - jit->prg;
			EMIT4_PCREL(0xa7f40000, offset);
			break;
		}
		if (filter->jt != 0) {
			/* brc	<mask>,<jt> */
			offset = addrs[i + filter->jt] + jit->start - jit->prg;
			EMIT4_PCREL(0xa7040000 | mask, offset);
		}
		if (filter->jf != 0) {
			/* brc	<mask^15>,<jf> */
			offset = addrs[i + filter->jf] + jit->start - jit->prg;
			EMIT4_PCREL(0xa7040000 | (mask ^ 0xf00000), offset);
		}
		break;
	case BPF_S_JMP_JSET_K: /* ip += (A & K) ? jt : jf */
		mask = 0x700000; /* jnz */
		/* Emit test if the branch targets are different */
		if (filter->jt != filter->jf) {
			if (K > 65535) {
				/* lr %r4,%r5 */
				EMIT2(0x1845);
				/* n %r4,<d(K)>(%r13) */
				EMIT4_DISP(0x5440d000, EMIT_CONST(K));
			} else
				/* tmll %r5,K */
				EMIT4_IMM(0xa7510000, K);
		}
		goto branch;
	case BPF_S_JMP_JGT_X: /* ip += (A > X) ? jt : jf */
		mask = 0x200000; /* jh */
		goto xbranch;
	case BPF_S_JMP_JGE_X: /* ip += (A >= X) ? jt : jf */
		mask = 0xa00000; /* jhe */
		goto xbranch;
	case BPF_S_JMP_JEQ_X: /* ip += (A == X) ? jt : jf */
		mask = 0x800000; /* je */
xbranch:	/* Emit compare if the branch targets are different */
		if (filter->jt != filter->jf) {
			jit->seen |= SEEN_XREG;
			/* cr %r5,%r12 */
			EMIT2(0x195c);
		}
		goto branch;
	case BPF_S_JMP_JSET_X: /* ip += (A & X) ? jt : jf */
		mask = 0x700000; /* jnz */
		/* Emit test if the branch targets are different */
		if (filter->jt != filter->jf) {
			jit->seen |= SEEN_XREG;
			/* lr %r4,%r5 */
			EMIT2(0x1845);
			/* nr %r4,%r12 */
			EMIT2(0x144c);
		}
		goto branch;
	case BPF_S_LD_W_ABS: /* A = *(u32 *) (skb->data+K) */
		jit->seen |= SEEN_DATAREF | SEEN_RET0 | SEEN_LOAD_WORD;
		offset = jit->off_load_word;
		goto load_abs;
	case BPF_S_LD_H_ABS: /* A = *(u16 *) (skb->data+K) */
		jit->seen |= SEEN_DATAREF | SEEN_RET0 | SEEN_LOAD_HALF;
		offset = jit->off_load_half;
		goto load_abs;
	case BPF_S_LD_B_ABS: /* A = *(u8 *) (skb->data+K) */
		jit->seen |= SEEN_DATAREF | SEEN_RET0 | SEEN_LOAD_BYTE;
		offset = jit->off_load_byte;
load_abs:	if ((int) K < 0)
			goto out;
call_fn:	/* lg %r1,<d(function)>(%r13) */
		EMIT6_DISP(0xe310d000, 0x0004, offset);
		/* l %r3,<d(K)>(%r13) */
		EMIT4_DISP(0x5830d000, EMIT_CONST(K));
		/* basr %r8,%r1 */
		EMIT2(0x0d81);
		/* jnz <ret0> */
		EMIT4_PCREL(0xa7740000, (jit->ret0_ip - jit->prg));
		break;
	case BPF_S_LD_W_IND: /* A = *(u32 *) (skb->data+K+X) */
		jit->seen |= SEEN_DATAREF | SEEN_RET0 | SEEN_LOAD_IWORD;
		offset = jit->off_load_iword;
		goto call_fn;
	case BPF_S_LD_H_IND: /* A = *(u16 *) (skb->data+K+X) */
		jit->seen |= SEEN_DATAREF | SEEN_RET0 | SEEN_LOAD_IHALF;
		offset = jit->off_load_ihalf;
		goto call_fn;
	case BPF_S_LD_B_IND: /* A = *(u8 *) (skb->data+K+X) */
		jit->seen |= SEEN_DATAREF | SEEN_RET0 | SEEN_LOAD_IBYTE;
		offset = jit->off_load_ibyte;
		goto call_fn;
	case BPF_S_LDX_B_MSH:
		/* X = (*(u8 *)(skb->data+K) & 0xf) << 2 */
		jit->seen |= SEEN_RET0;
		if ((int) K < 0) {
			/* j <ret0> */
			EMIT4_PCREL(0xa7f40000, (jit->ret0_ip - jit->prg));
			break;
		}
		jit->seen |= SEEN_DATAREF | SEEN_LOAD_BMSH;
		offset = jit->off_load_bmsh;
		goto call_fn;
	case BPF_S_LD_W_LEN: /*	A = skb->len; */
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, len) != 4);
		/* l %r5,<d(len)>(%r2) */
		EMIT4_DISP(0x58502000, offsetof(struct sk_buff, len));
		break;
	case BPF_S_LDX_W_LEN: /* X = skb->len; */
		jit->seen |= SEEN_XREG;
		/* l %r12,<d(len)>(%r2) */
		EMIT4_DISP(0x58c02000, offsetof(struct sk_buff, len));
		break;
	case BPF_S_LD_IMM: /* A = K */
		if (K <= 16383)
			/* lhi %r5,K */
			EMIT4_IMM(0xa7580000, K);
		else if (test_facility(21))
			/* llilf %r5,<K> */
			EMIT6_IMM(0xc05f0000, K);
		else
			/* l %r5,<d(K)>(%r13) */
			EMIT4_DISP(0x5850d000, EMIT_CONST(K));
		break;
	case BPF_S_LDX_IMM: /* X = K */
		jit->seen |= SEEN_XREG;
		if (K <= 16383)
			/* lhi %r12,<K> */
			EMIT4_IMM(0xa7c80000, K);
		else if (test_facility(21))
			/* llilf %r12,<K> */
			EMIT6_IMM(0xc0cf0000, K);
		else
			/* l %r12,<d(K)>(%r13) */
			EMIT4_DISP(0x58c0d000, EMIT_CONST(K));
		break;
	case BPF_S_LD_MEM: /* A = mem[K] */
		jit->seen |= SEEN_MEM;
		/* l %r5,<K>(%r15) */
		EMIT4_DISP(0x5850f000,
			   (jit->seen & SEEN_DATAREF) ? 160 + K*4 : K*4);
		break;
	case BPF_S_LDX_MEM: /* X = mem[K] */
		jit->seen |= SEEN_XREG | SEEN_MEM;
		/* l %r12,<K>(%r15) */
		EMIT4_DISP(0x58c0f000,
			   (jit->seen & SEEN_DATAREF) ? 160 + K*4 : K*4);
		break;
	case BPF_S_MISC_TAX: /* X = A */
		jit->seen |= SEEN_XREG;
		/* lr %r12,%r5 */
		EMIT2(0x18c5);
		break;
	case BPF_S_MISC_TXA: /* A = X */
		jit->seen |= SEEN_XREG;
		/* lr %r5,%r12 */
		EMIT2(0x185c);
		break;
	case BPF_S_RET_K:
		if (K == 0) {
			jit->seen |= SEEN_RET0;
			if (last)
				break;
			/* j <ret0> */
			EMIT4_PCREL(0xa7f40000, jit->ret0_ip - jit->prg);
		} else {
			if (K <= 16383)
				/* lghi %r2,K */
				EMIT4_IMM(0xa7290000, K);
			else
				/* llgf %r2,<K>(%r13) */
				EMIT6_DISP(0xe320d000, 0x0016, EMIT_CONST(K));
			/* j <exit> */
			if (last && !(jit->seen & SEEN_RET0))
				break;
			EMIT4_PCREL(0xa7f40000, jit->exit_ip - jit->prg);
		}
		break;
	case BPF_S_RET_A:
		/* llgfr %r2,%r5 */
		EMIT4(0xb9160025);
		/* j <exit> */
		EMIT4_PCREL(0xa7f40000, jit->exit_ip - jit->prg);
		break;
	case BPF_S_ST: /* mem[K] = A */
		jit->seen |= SEEN_MEM;
		/* st %r5,<K>(%r15) */
		EMIT4_DISP(0x5050f000,
			   (jit->seen & SEEN_DATAREF) ? 160 + K*4 : K*4);
		break;
	case BPF_S_STX: /* mem[K] = X : mov %ebx,off8(%rbp) */
		jit->seen |= SEEN_XREG | SEEN_MEM;
		/* st %r12,<K>(%r15) */
		EMIT4_DISP(0x50c0f000,
			   (jit->seen & SEEN_DATAREF) ? 160 + K*4 : K*4);
		break;
	case BPF_S_ANC_PROTOCOL: /* A = ntohs(skb->protocol); */
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, protocol) != 2);
		/* lhi %r5,0 */
		EMIT4(0xa7580000);
		/* icm	%r5,3,<d(protocol)>(%r2) */
		EMIT4_DISP(0xbf532000, offsetof(struct sk_buff, protocol));
		break;
	case BPF_S_ANC_IFINDEX:	/* if (!skb->dev) return 0;
				 * A = skb->dev->ifindex */
		BUILD_BUG_ON(FIELD_SIZEOF(struct net_device, ifindex) != 4);
		jit->seen |= SEEN_RET0;
		/* lg %r1,<d(dev)>(%r2) */
		EMIT6_DISP(0xe3102000, 0x0004, offsetof(struct sk_buff, dev));
		/* ltgr %r1,%r1 */
		EMIT4(0xb9020011);
		/* jz <ret0> */
		EMIT4_PCREL(0xa7840000, jit->ret0_ip - jit->prg);
		/* l %r5,<d(ifindex)>(%r1) */
		EMIT4_DISP(0x58501000, offsetof(struct net_device, ifindex));
		break;
	case BPF_S_ANC_MARK: /* A = skb->mark */
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, mark) != 4);
		/* l %r5,<d(mark)>(%r2) */
		EMIT4_DISP(0x58502000, offsetof(struct sk_buff, mark));
		break;
	case BPF_S_ANC_QUEUE: /* A = skb->queue_mapping */
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, queue_mapping) != 2);
		/* lhi %r5,0 */
		EMIT4(0xa7580000);
		/* icm	%r5,3,<d(queue_mapping)>(%r2) */
		EMIT4_DISP(0xbf532000, offsetof(struct sk_buff, queue_mapping));
		break;
	case BPF_S_ANC_HATYPE:	/* if (!skb->dev) return 0;
				 * A = skb->dev->type */
		BUILD_BUG_ON(FIELD_SIZEOF(struct net_device, type) != 2);
		jit->seen |= SEEN_RET0;
		/* lg %r1,<d(dev)>(%r2) */
		EMIT6_DISP(0xe3102000, 0x0004, offsetof(struct sk_buff, dev));
		/* ltgr %r1,%r1 */
		EMIT4(0xb9020011);
		/* jz <ret0> */
		EMIT4_PCREL(0xa7840000, jit->ret0_ip - jit->prg);
		/* lhi %r5,0 */
		EMIT4(0xa7580000);
		/* icm	%r5,3,<d(type)>(%r1) */
		EMIT4_DISP(0xbf531000, offsetof(struct net_device, type));
		break;
	case BPF_S_ANC_RXHASH: /* A = skb->rxhash */
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, rxhash) != 4);
		/* l %r5,<d(rxhash)>(%r2) */
		EMIT4_DISP(0x58502000, offsetof(struct sk_buff, rxhash));
		break;
	case BPF_S_ANC_VLAN_TAG:
	case BPF_S_ANC_VLAN_TAG_PRESENT:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, vlan_tci) != 2);
		BUILD_BUG_ON(VLAN_TAG_PRESENT != 0x1000);
		/* lhi %r5,0 */
		EMIT4(0xa7580000);
		/* icm	%r5,3,<d(vlan_tci)>(%r2) */
		EMIT4_DISP(0xbf532000, offsetof(struct sk_buff, vlan_tci));
		if (filter->code == BPF_S_ANC_VLAN_TAG) {
			/* nill %r5,0xefff */
			EMIT4_IMM(0xa5570000, ~VLAN_TAG_PRESENT);
		} else {
			/* nill %r5,0x1000 */
			EMIT4_IMM(0xa5570000, VLAN_TAG_PRESENT);
			/* srl %r5,12 */
			EMIT4_DISP(0x88500000, 12);
		}
		break;
	case BPF_S_ANC_PKTTYPE:
		if (pkt_type_offset < 0)
			goto out;
		/* lhi %r5,0 */
		EMIT4(0xa7580000);
		/* ic %r5,<d(pkt_type_offset)>(%r2) */
		EMIT4_DISP(0x43502000, pkt_type_offset);
		/* srl %r5,5 */
		EMIT4_DISP(0x88500000, 5);
		break;
	case BPF_S_ANC_CPU: /* A = smp_processor_id() */
#ifdef CONFIG_SMP
		/* l %r5,<d(cpu_nr)> */
		EMIT4_DISP(0x58500000, offsetof(struct _lowcore, cpu_nr));
#else
		/* lhi %r5,0 */
		EMIT4(0xa7580000);
#endif
		break;
	default: /* too complex, give up */
		goto out;
	}
	addrs[i] = jit->prg - jit->start;
	return 0;
out:
	return -1;
}

/*
 * Note: for security reasons, bpf code will follow a randomly
 *	 sized amount of illegal instructions.
 */
struct bpf_binary_header {
	unsigned int pages;
	u8 image[];
};

static struct bpf_binary_header *bpf_alloc_binary(unsigned int bpfsize,
						  u8 **image_ptr)
{
	struct bpf_binary_header *header;
	unsigned int sz, hole;

	/* Most BPF filters are really small, but if some of them fill a page,
	 * allow at least 128 extra bytes for illegal instructions.
	 */
	sz = round_up(bpfsize + sizeof(*header) + 128, PAGE_SIZE);
	header = module_alloc(sz);
	if (!header)
		return NULL;
	memset(header, 0, sz);
	header->pages = sz / PAGE_SIZE;
	hole = sz - (bpfsize + sizeof(*header));
	/* Insert random number of illegal instructions before BPF code
	 * and make sure the first instruction starts at an even address.
	 */
	*image_ptr = &header->image[(prandom_u32() % hole) & -2];
	return header;
}

void bpf_jit_compile(struct sk_filter *fp)
{
	struct bpf_binary_header *header = NULL;
	unsigned long size, prg_len, lit_len;
	struct bpf_jit jit, cjit;
	unsigned int *addrs;
	int pass, i;

	if (!bpf_jit_enable)
		return;
	addrs = kcalloc(fp->len, sizeof(*addrs), GFP_KERNEL);
	if (addrs == NULL)
		return;
	memset(&jit, 0, sizeof(cjit));
	memset(&cjit, 0, sizeof(cjit));

	for (pass = 0; pass < 10; pass++) {
		jit.prg = jit.start;
		jit.lit = jit.mid;

		bpf_jit_prologue(&jit);
		bpf_jit_noleaks(&jit, fp->insns);
		for (i = 0; i < fp->len; i++) {
			if (bpf_jit_insn(&jit, fp->insns + i, addrs, i,
					 i == fp->len - 1))
				goto out;
		}
		bpf_jit_epilogue(&jit);
		if (jit.start) {
			WARN_ON(jit.prg > cjit.prg || jit.lit > cjit.lit);
			if (memcmp(&jit, &cjit, sizeof(jit)) == 0)
				break;
		} else if (jit.prg == cjit.prg && jit.lit == cjit.lit) {
			prg_len = jit.prg - jit.start;
			lit_len = jit.lit - jit.mid;
			size = prg_len + lit_len;
			if (size >= BPF_SIZE_MAX)
				goto out;
			header = bpf_alloc_binary(size, &jit.start);
			if (!header)
				goto out;
			jit.prg = jit.mid = jit.start + prg_len;
			jit.lit = jit.end = jit.start + prg_len + lit_len;
			jit.base_ip += (unsigned long) jit.start;
			jit.exit_ip += (unsigned long) jit.start;
			jit.ret0_ip += (unsigned long) jit.start;
		}
		cjit = jit;
	}
	if (bpf_jit_enable > 1) {
		bpf_jit_dump(fp->len, jit.end - jit.start, pass, jit.start);
		if (jit.start)
			print_fn_code(jit.start, jit.mid - jit.start);
	}
	if (jit.start) {
		set_memory_ro((unsigned long)header, header->pages);
		fp->bpf_func = (void *) jit.start;
	}
out:
	kfree(addrs);
}

void bpf_jit_free(struct sk_filter *fp)
{
	unsigned long addr = (unsigned long)fp->bpf_func & PAGE_MASK;
	struct bpf_binary_header *header = (void *)addr;

	if (fp->bpf_func == sk_run_filter)
		return;
	set_memory_rw(addr, header->pages);
	module_free(NULL, header);
}
