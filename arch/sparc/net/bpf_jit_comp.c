#include <linux/moduleloader.h>
#include <linux/workqueue.h>
#include <linux/netdevice.h>
#include <linux/filter.h>
#include <linux/cache.h>
#include <linux/if_vlan.h>

#include <asm/cacheflush.h>
#include <asm/ptrace.h>

#include "bpf_jit.h"

int bpf_jit_enable __read_mostly;

static inline bool is_simm13(unsigned int value)
{
	return value + 0x1000 < 0x2000;
}

static void bpf_flush_icache(void *start_, void *end_)
{
#ifdef CONFIG_SPARC64
	/* Cheetah's I-cache is fully coherent.  */
	if (tlb_type == spitfire) {
		unsigned long start = (unsigned long) start_;
		unsigned long end = (unsigned long) end_;

		start &= ~7UL;
		end = (end + 7UL) & ~7UL;
		while (start < end) {
			flushi(start);
			start += 32;
		}
	}
#endif
}

#define SEEN_DATAREF 1 /* might call external helpers */
#define SEEN_XREG    2 /* ebx is used */
#define SEEN_MEM     4 /* use mem[] for temporary storage */

#define S13(X)		((X) & 0x1fff)
#define IMMED		0x00002000
#define RD(X)		((X) << 25)
#define RS1(X)		((X) << 14)
#define RS2(X)		((X))
#define OP(X)		((X) << 30)
#define OP2(X)		((X) << 22)
#define OP3(X)		((X) << 19)
#define COND(X)		((X) << 25)
#define F1(X)		OP(X)
#define F2(X, Y)	(OP(X) | OP2(Y))
#define F3(X, Y)	(OP(X) | OP3(Y))

#define CONDN		COND(0x0)
#define CONDE		COND(0x1)
#define CONDLE		COND(0x2)
#define CONDL		COND(0x3)
#define CONDLEU		COND(0x4)
#define CONDCS		COND(0x5)
#define CONDNEG		COND(0x6)
#define CONDVC		COND(0x7)
#define CONDA		COND(0x8)
#define CONDNE		COND(0x9)
#define CONDG		COND(0xa)
#define CONDGE		COND(0xb)
#define CONDGU		COND(0xc)
#define CONDCC		COND(0xd)
#define CONDPOS		COND(0xe)
#define CONDVS		COND(0xf)

#define CONDGEU		CONDCC
#define CONDLU		CONDCS

#define WDISP22(X)	(((X) >> 2) & 0x3fffff)

#define BA		(F2(0, 2) | CONDA)
#define BGU		(F2(0, 2) | CONDGU)
#define BLEU		(F2(0, 2) | CONDLEU)
#define BGEU		(F2(0, 2) | CONDGEU)
#define BLU		(F2(0, 2) | CONDLU)
#define BE		(F2(0, 2) | CONDE)
#define BNE		(F2(0, 2) | CONDNE)

#ifdef CONFIG_SPARC64
#define BE_PTR		(F2(0, 1) | CONDE | (2 << 20))
#else
#define BE_PTR		BE
#endif

#define SETHI(K, REG)	\
	(F2(0, 0x4) | RD(REG) | (((K) >> 10) & 0x3fffff))
#define OR_LO(K, REG)	\
	(F3(2, 0x02) | IMMED | RS1(REG) | ((K) & 0x3ff) | RD(REG))

#define ADD		F3(2, 0x00)
#define AND		F3(2, 0x01)
#define ANDCC		F3(2, 0x11)
#define OR		F3(2, 0x02)
#define XOR		F3(2, 0x03)
#define SUB		F3(2, 0x04)
#define SUBCC		F3(2, 0x14)
#define MUL		F3(2, 0x0a)	/* umul */
#define DIV		F3(2, 0x0e)	/* udiv */
#define SLL		F3(2, 0x25)
#define SRL		F3(2, 0x26)
#define JMPL		F3(2, 0x38)
#define CALL		F1(1)
#define BR		F2(0, 0x01)
#define RD_Y		F3(2, 0x28)
#define WR_Y		F3(2, 0x30)

#define LD32		F3(3, 0x00)
#define LD8		F3(3, 0x01)
#define LD16		F3(3, 0x02)
#define LD64		F3(3, 0x0b)
#define ST32		F3(3, 0x04)

#ifdef CONFIG_SPARC64
#define LDPTR		LD64
#define BASE_STACKFRAME	176
#else
#define LDPTR		LD32
#define BASE_STACKFRAME	96
#endif

#define LD32I		(LD32 | IMMED)
#define LD8I		(LD8 | IMMED)
#define LD16I		(LD16 | IMMED)
#define LD64I		(LD64 | IMMED)
#define LDPTRI		(LDPTR | IMMED)
#define ST32I		(ST32 | IMMED)

#define emit_nop()		\
do {				\
	*prog++ = SETHI(0, G0);	\
} while (0)

#define emit_neg()					\
do {	/* sub %g0, r_A, r_A */				\
	*prog++ = SUB | RS1(G0) | RS2(r_A) | RD(r_A);	\
} while (0)

#define emit_reg_move(FROM, TO)				\
do {	/* or %g0, FROM, TO */				\
	*prog++ = OR | RS1(G0) | RS2(FROM) | RD(TO);	\
} while (0)

#define emit_clear(REG)					\
do {	/* or %g0, %g0, REG */				\
	*prog++ = OR | RS1(G0) | RS2(G0) | RD(REG);	\
} while (0)

#define emit_set_const(K, REG)					\
do {	/* sethi %hi(K), REG */					\
	*prog++ = SETHI(K, REG);				\
	/* or REG, %lo(K), REG */				\
	*prog++ = OR_LO(K, REG);				\
} while (0)

	/* Emit
	 *
	 *	OP	r_A, r_X, r_A
	 */
#define emit_alu_X(OPCODE)					\
do {								\
	seen |= SEEN_XREG;					\
	*prog++ = OPCODE | RS1(r_A) | RS2(r_X) | RD(r_A);	\
} while (0)

	/* Emit either:
	 *
	 *	OP	r_A, K, r_A
	 *
	 * or
	 *
	 *	sethi	%hi(K), r_TMP
	 *	or	r_TMP, %lo(K), r_TMP
	 *	OP	r_A, r_TMP, r_A
	 *
	 * depending upon whether K fits in a signed 13-bit
	 * immediate instruction field.  Emit nothing if K
	 * is zero.
	 */
#define emit_alu_K(OPCODE, K)					\
do {								\
	if (K || OPCODE == AND || OPCODE == MUL) {		\
		unsigned int _insn = OPCODE;			\
		_insn |= RS1(r_A) | RD(r_A);			\
		if (is_simm13(K)) {				\
			*prog++ = _insn | IMMED | S13(K);	\
		} else {					\
			emit_set_const(K, r_TMP);		\
			*prog++ = _insn | RS2(r_TMP);		\
		}						\
	}							\
} while (0)

#define emit_loadimm(K, DEST)						\
do {									\
	if (is_simm13(K)) {						\
		/* or %g0, K, DEST */					\
		*prog++ = OR | IMMED | RS1(G0) | S13(K) | RD(DEST);	\
	} else {							\
		emit_set_const(K, DEST);				\
	}								\
} while (0)

#define emit_loadptr(BASE, STRUCT, FIELD, DEST)				\
do {	unsigned int _off = offsetof(STRUCT, FIELD);			\
	BUILD_BUG_ON(FIELD_SIZEOF(STRUCT, FIELD) != sizeof(void *));	\
	*prog++ = LDPTRI | RS1(BASE) | S13(_off) | RD(DEST);		\
} while (0)

#define emit_load32(BASE, STRUCT, FIELD, DEST)				\
do {	unsigned int _off = offsetof(STRUCT, FIELD);			\
	BUILD_BUG_ON(FIELD_SIZEOF(STRUCT, FIELD) != sizeof(u32));	\
	*prog++ = LD32I | RS1(BASE) | S13(_off) | RD(DEST);		\
} while (0)

#define emit_load16(BASE, STRUCT, FIELD, DEST)				\
do {	unsigned int _off = offsetof(STRUCT, FIELD);			\
	BUILD_BUG_ON(FIELD_SIZEOF(STRUCT, FIELD) != sizeof(u16));	\
	*prog++ = LD16I | RS1(BASE) | S13(_off) | RD(DEST);		\
} while (0)

#define __emit_load8(BASE, STRUCT, FIELD, DEST)				\
do {	unsigned int _off = offsetof(STRUCT, FIELD);			\
	*prog++ = LD8I | RS1(BASE) | S13(_off) | RD(DEST);		\
} while (0)

#define emit_load8(BASE, STRUCT, FIELD, DEST)				\
do {	BUILD_BUG_ON(FIELD_SIZEOF(STRUCT, FIELD) != sizeof(u8));	\
	__emit_load8(BASE, STRUCT, FIELD, DEST);			\
} while (0)

#ifdef CONFIG_SPARC64
#define BIAS (STACK_BIAS - 4)
#else
#define BIAS (-4)
#endif

#define emit_ldmem(OFF, DEST)						\
do {	*prog++ = LD32I | RS1(SP) | S13(BIAS - (OFF)) | RD(DEST);	\
} while (0)

#define emit_stmem(OFF, SRC)						\
do {	*prog++ = ST32I | RS1(SP) | S13(BIAS - (OFF)) | RD(SRC);	\
} while (0)

#ifdef CONFIG_SMP
#ifdef CONFIG_SPARC64
#define emit_load_cpu(REG)						\
	emit_load16(G6, struct thread_info, cpu, REG)
#else
#define emit_load_cpu(REG)						\
	emit_load32(G6, struct thread_info, cpu, REG)
#endif
#else
#define emit_load_cpu(REG)	emit_clear(REG)
#endif

#define emit_skb_loadptr(FIELD, DEST) \
	emit_loadptr(r_SKB, struct sk_buff, FIELD, DEST)
#define emit_skb_load32(FIELD, DEST) \
	emit_load32(r_SKB, struct sk_buff, FIELD, DEST)
#define emit_skb_load16(FIELD, DEST) \
	emit_load16(r_SKB, struct sk_buff, FIELD, DEST)
#define __emit_skb_load8(FIELD, DEST) \
	__emit_load8(r_SKB, struct sk_buff, FIELD, DEST)
#define emit_skb_load8(FIELD, DEST) \
	emit_load8(r_SKB, struct sk_buff, FIELD, DEST)

#define emit_jmpl(BASE, IMM_OFF, LREG) \
	*prog++ = (JMPL | IMMED | RS1(BASE) | S13(IMM_OFF) | RD(LREG))

#define emit_call(FUNC)					\
do {	void *_here = image + addrs[i] - 8;		\
	unsigned int _off = (void *)(FUNC) - _here;	\
	*prog++ = CALL | (((_off) >> 2) & 0x3fffffff);	\
	emit_nop();					\
} while (0)

#define emit_branch(BR_OPC, DEST)			\
do {	unsigned int _here = addrs[i] - 8;		\
	*prog++ = BR_OPC | WDISP22((DEST) - _here);	\
} while (0)

#define emit_branch_off(BR_OPC, OFF)			\
do {	*prog++ = BR_OPC | WDISP22(OFF);		\
} while (0)

#define emit_jump(DEST)		emit_branch(BA, DEST)

#define emit_read_y(REG)	*prog++ = RD_Y | RD(REG)
#define emit_write_y(REG)	*prog++ = WR_Y | IMMED | RS1(REG) | S13(0)

#define emit_cmp(R1, R2) \
	*prog++ = (SUBCC | RS1(R1) | RS2(R2) | RD(G0))

#define emit_cmpi(R1, IMM) \
	*prog++ = (SUBCC | IMMED | RS1(R1) | S13(IMM) | RD(G0));

#define emit_btst(R1, R2) \
	*prog++ = (ANDCC | RS1(R1) | RS2(R2) | RD(G0))

#define emit_btsti(R1, IMM) \
	*prog++ = (ANDCC | IMMED | RS1(R1) | S13(IMM) | RD(G0));

#define emit_sub(R1, R2, R3) \
	*prog++ = (SUB | RS1(R1) | RS2(R2) | RD(R3))

#define emit_subi(R1, IMM, R3) \
	*prog++ = (SUB | IMMED | RS1(R1) | S13(IMM) | RD(R3))

#define emit_add(R1, R2, R3) \
	*prog++ = (ADD | RS1(R1) | RS2(R2) | RD(R3))

#define emit_addi(R1, IMM, R3) \
	*prog++ = (ADD | IMMED | RS1(R1) | S13(IMM) | RD(R3))

#define emit_and(R1, R2, R3) \
	*prog++ = (AND | RS1(R1) | RS2(R2) | RD(R3))

#define emit_andi(R1, IMM, R3) \
	*prog++ = (AND | IMMED | RS1(R1) | S13(IMM) | RD(R3))

#define emit_alloc_stack(SZ) \
	*prog++ = (SUB | IMMED | RS1(SP) | S13(SZ) | RD(SP))

#define emit_release_stack(SZ) \
	*prog++ = (ADD | IMMED | RS1(SP) | S13(SZ) | RD(SP))

/* A note about branch offset calculations.  The addrs[] array,
 * indexed by BPF instruction, records the address after all the
 * sparc instructions emitted for that BPF instruction.
 *
 * The most common case is to emit a branch at the end of such
 * a code sequence.  So this would be two instructions, the
 * branch and it's delay slot.
 *
 * Therefore by default the branch emitters calculate the branch
 * offset field as:
 *
 *	destination - (addrs[i] - 8)
 *
 * This "addrs[i] - 8" is the address of the branch itself or
 * what "." would be in assembler notation.  The "8" part is
 * how we take into consideration the branch and it's delay
 * slot mentioned above.
 *
 * Sometimes we need to emit a branch earlier in the code
 * sequence.  And in these situations we adjust "destination"
 * to accomodate this difference.  For example, if we needed
 * to emit a branch (and it's delay slot) right before the
 * final instruction emitted for a BPF opcode, we'd use
 * "destination + 4" instead of just plain "destination" above.
 *
 * This is why you see all of these funny emit_branch() and
 * emit_jump() calls with adjusted offsets.
 */

void bpf_jit_compile(struct bpf_prog *fp)
{
	unsigned int cleanup_addr, proglen, oldproglen = 0;
	u32 temp[8], *prog, *func, seen = 0, pass;
	const struct sock_filter *filter = fp->insns;
	int i, flen = fp->len, pc_ret0 = -1;
	unsigned int *addrs;
	void *image;

	if (!bpf_jit_enable)
		return;

	addrs = kmalloc(flen * sizeof(*addrs), GFP_KERNEL);
	if (addrs == NULL)
		return;

	/* Before first pass, make a rough estimation of addrs[]
	 * each bpf instruction is translated to less than 64 bytes
	 */
	for (proglen = 0, i = 0; i < flen; i++) {
		proglen += 64;
		addrs[i] = proglen;
	}
	cleanup_addr = proglen; /* epilogue address */
	image = NULL;
	for (pass = 0; pass < 10; pass++) {
		u8 seen_or_pass0 = (pass == 0) ? (SEEN_XREG | SEEN_DATAREF | SEEN_MEM) : seen;

		/* no prologue/epilogue for trivial filters (RET something) */
		proglen = 0;
		prog = temp;

		/* Prologue */
		if (seen_or_pass0) {
			if (seen_or_pass0 & SEEN_MEM) {
				unsigned int sz = BASE_STACKFRAME;
				sz += BPF_MEMWORDS * sizeof(u32);
				emit_alloc_stack(sz);
			}

			/* Make sure we dont leek kernel memory. */
			if (seen_or_pass0 & SEEN_XREG)
				emit_clear(r_X);

			/* If this filter needs to access skb data,
			 * load %o4 and %o5 with:
			 *  %o4 = skb->len - skb->data_len
			 *  %o5 = skb->data
			 * And also back up %o7 into r_saved_O7 so we can
			 * invoke the stubs using 'call'.
			 */
			if (seen_or_pass0 & SEEN_DATAREF) {
				emit_load32(r_SKB, struct sk_buff, len, r_HEADLEN);
				emit_load32(r_SKB, struct sk_buff, data_len, r_TMP);
				emit_sub(r_HEADLEN, r_TMP, r_HEADLEN);
				emit_loadptr(r_SKB, struct sk_buff, data, r_SKB_DATA);
			}
		}
		emit_reg_move(O7, r_saved_O7);

		switch (filter[0].code) {
		case BPF_RET | BPF_K:
		case BPF_LD | BPF_W | BPF_LEN:
		case BPF_LD | BPF_W | BPF_ABS:
		case BPF_LD | BPF_H | BPF_ABS:
		case BPF_LD | BPF_B | BPF_ABS:
			/* The first instruction sets the A register (or is
			 * a "RET 'constant'")
			 */
			break;
		default:
			/* Make sure we dont leak kernel information to the
			 * user.
			 */
			emit_clear(r_A); /* A = 0 */
		}

		for (i = 0; i < flen; i++) {
			unsigned int K = filter[i].k;
			unsigned int t_offset;
			unsigned int f_offset;
			u32 t_op, f_op;
			u16 code = bpf_anc_helper(&filter[i]);
			int ilen;

			switch (code) {
			case BPF_ALU | BPF_ADD | BPF_X:	/* A += X; */
				emit_alu_X(ADD);
				break;
			case BPF_ALU | BPF_ADD | BPF_K:	/* A += K; */
				emit_alu_K(ADD, K);
				break;
			case BPF_ALU | BPF_SUB | BPF_X:	/* A -= X; */
				emit_alu_X(SUB);
				break;
			case BPF_ALU | BPF_SUB | BPF_K:	/* A -= K */
				emit_alu_K(SUB, K);
				break;
			case BPF_ALU | BPF_AND | BPF_X:	/* A &= X */
				emit_alu_X(AND);
				break;
			case BPF_ALU | BPF_AND | BPF_K:	/* A &= K */
				emit_alu_K(AND, K);
				break;
			case BPF_ALU | BPF_OR | BPF_X:	/* A |= X */
				emit_alu_X(OR);
				break;
			case BPF_ALU | BPF_OR | BPF_K:	/* A |= K */
				emit_alu_K(OR, K);
				break;
			case BPF_ANC | SKF_AD_ALU_XOR_X: /* A ^= X; */
			case BPF_ALU | BPF_XOR | BPF_X:
				emit_alu_X(XOR);
				break;
			case BPF_ALU | BPF_XOR | BPF_K:	/* A ^= K */
				emit_alu_K(XOR, K);
				break;
			case BPF_ALU | BPF_LSH | BPF_X:	/* A <<= X */
				emit_alu_X(SLL);
				break;
			case BPF_ALU | BPF_LSH | BPF_K:	/* A <<= K */
				emit_alu_K(SLL, K);
				break;
			case BPF_ALU | BPF_RSH | BPF_X:	/* A >>= X */
				emit_alu_X(SRL);
				break;
			case BPF_ALU | BPF_RSH | BPF_K:	/* A >>= K */
				emit_alu_K(SRL, K);
				break;
			case BPF_ALU | BPF_MUL | BPF_X:	/* A *= X; */
				emit_alu_X(MUL);
				break;
			case BPF_ALU | BPF_MUL | BPF_K:	/* A *= K */
				emit_alu_K(MUL, K);
				break;
			case BPF_ALU | BPF_DIV | BPF_K:	/* A /= K with K != 0*/
				if (K == 1)
					break;
				emit_write_y(G0);
#ifdef CONFIG_SPARC32
				/* The Sparc v8 architecture requires
				 * three instructions between a %y
				 * register write and the first use.
				 */
				emit_nop();
				emit_nop();
				emit_nop();
#endif
				emit_alu_K(DIV, K);
				break;
			case BPF_ALU | BPF_DIV | BPF_X:	/* A /= X; */
				emit_cmpi(r_X, 0);
				if (pc_ret0 > 0) {
					t_offset = addrs[pc_ret0 - 1];
#ifdef CONFIG_SPARC32
					emit_branch(BE, t_offset + 20);
#else
					emit_branch(BE, t_offset + 8);
#endif
					emit_nop(); /* delay slot */
				} else {
					emit_branch_off(BNE, 16);
					emit_nop();
#ifdef CONFIG_SPARC32
					emit_jump(cleanup_addr + 20);
#else
					emit_jump(cleanup_addr + 8);
#endif
					emit_clear(r_A);
				}
				emit_write_y(G0);
#ifdef CONFIG_SPARC32
				/* The Sparc v8 architecture requires
				 * three instructions between a %y
				 * register write and the first use.
				 */
				emit_nop();
				emit_nop();
				emit_nop();
#endif
				emit_alu_X(DIV);
				break;
			case BPF_ALU | BPF_NEG:
				emit_neg();
				break;
			case BPF_RET | BPF_K:
				if (!K) {
					if (pc_ret0 == -1)
						pc_ret0 = i;
					emit_clear(r_A);
				} else {
					emit_loadimm(K, r_A);
				}
				/* Fallthrough */
			case BPF_RET | BPF_A:
				if (seen_or_pass0) {
					if (i != flen - 1) {
						emit_jump(cleanup_addr);
						emit_nop();
						break;
					}
					if (seen_or_pass0 & SEEN_MEM) {
						unsigned int sz = BASE_STACKFRAME;
						sz += BPF_MEMWORDS * sizeof(u32);
						emit_release_stack(sz);
					}
				}
				/* jmpl %r_saved_O7 + 8, %g0 */
				emit_jmpl(r_saved_O7, 8, G0);
				emit_reg_move(r_A, O0); /* delay slot */
				break;
			case BPF_MISC | BPF_TAX:
				seen |= SEEN_XREG;
				emit_reg_move(r_A, r_X);
				break;
			case BPF_MISC | BPF_TXA:
				seen |= SEEN_XREG;
				emit_reg_move(r_X, r_A);
				break;
			case BPF_ANC | SKF_AD_CPU:
				emit_load_cpu(r_A);
				break;
			case BPF_ANC | SKF_AD_PROTOCOL:
				emit_skb_load16(protocol, r_A);
				break;
#if 0
				/* GCC won't let us take the address of
				 * a bit field even though we very much
				 * know what we are doing here.
				 */
			case BPF_ANC | SKF_AD_PKTTYPE:
				__emit_skb_load8(pkt_type, r_A);
				emit_alu_K(SRL, 5);
				break;
#endif
			case BPF_ANC | SKF_AD_IFINDEX:
				emit_skb_loadptr(dev, r_A);
				emit_cmpi(r_A, 0);
				emit_branch(BE_PTR, cleanup_addr + 4);
				emit_nop();
				emit_load32(r_A, struct net_device, ifindex, r_A);
				break;
			case BPF_ANC | SKF_AD_MARK:
				emit_skb_load32(mark, r_A);
				break;
			case BPF_ANC | SKF_AD_QUEUE:
				emit_skb_load16(queue_mapping, r_A);
				break;
			case BPF_ANC | SKF_AD_HATYPE:
				emit_skb_loadptr(dev, r_A);
				emit_cmpi(r_A, 0);
				emit_branch(BE_PTR, cleanup_addr + 4);
				emit_nop();
				emit_load16(r_A, struct net_device, type, r_A);
				break;
			case BPF_ANC | SKF_AD_RXHASH:
				emit_skb_load32(hash, r_A);
				break;
			case BPF_ANC | SKF_AD_VLAN_TAG:
			case BPF_ANC | SKF_AD_VLAN_TAG_PRESENT:
				emit_skb_load16(vlan_tci, r_A);
				if (code != (BPF_ANC | SKF_AD_VLAN_TAG)) {
					emit_alu_K(SRL, 12);
					emit_andi(r_A, 1, r_A);
				} else {
					emit_loadimm(~VLAN_TAG_PRESENT, r_TMP);
					emit_and(r_A, r_TMP, r_A);
				}
				break;

			case BPF_LD | BPF_IMM:
				emit_loadimm(K, r_A);
				break;
			case BPF_LDX | BPF_IMM:
				emit_loadimm(K, r_X);
				break;
			case BPF_LD | BPF_MEM:
				seen |= SEEN_MEM;
				emit_ldmem(K * 4, r_A);
				break;
			case BPF_LDX | BPF_MEM:
				seen |= SEEN_MEM | SEEN_XREG;
				emit_ldmem(K * 4, r_X);
				break;
			case BPF_ST:
				seen |= SEEN_MEM;
				emit_stmem(K * 4, r_A);
				break;
			case BPF_STX:
				seen |= SEEN_MEM | SEEN_XREG;
				emit_stmem(K * 4, r_X);
				break;

#define CHOOSE_LOAD_FUNC(K, func) \
	((int)K < 0 ? ((int)K >= SKF_LL_OFF ? func##_negative_offset : func) : func##_positive_offset)

			case BPF_LD | BPF_W | BPF_ABS:
				func = CHOOSE_LOAD_FUNC(K, bpf_jit_load_word);
common_load:			seen |= SEEN_DATAREF;
				emit_loadimm(K, r_OFF);
				emit_call(func);
				break;
			case BPF_LD | BPF_H | BPF_ABS:
				func = CHOOSE_LOAD_FUNC(K, bpf_jit_load_half);
				goto common_load;
			case BPF_LD | BPF_B | BPF_ABS:
				func = CHOOSE_LOAD_FUNC(K, bpf_jit_load_byte);
				goto common_load;
			case BPF_LDX | BPF_B | BPF_MSH:
				func = CHOOSE_LOAD_FUNC(K, bpf_jit_load_byte_msh);
				goto common_load;
			case BPF_LD | BPF_W | BPF_IND:
				func = bpf_jit_load_word;
common_load_ind:		seen |= SEEN_DATAREF | SEEN_XREG;
				if (K) {
					if (is_simm13(K)) {
						emit_addi(r_X, K, r_OFF);
					} else {
						emit_loadimm(K, r_TMP);
						emit_add(r_X, r_TMP, r_OFF);
					}
				} else {
					emit_reg_move(r_X, r_OFF);
				}
				emit_call(func);
				break;
			case BPF_LD | BPF_H | BPF_IND:
				func = bpf_jit_load_half;
				goto common_load_ind;
			case BPF_LD | BPF_B | BPF_IND:
				func = bpf_jit_load_byte;
				goto common_load_ind;
			case BPF_JMP | BPF_JA:
				emit_jump(addrs[i + K]);
				emit_nop();
				break;

#define COND_SEL(CODE, TOP, FOP)	\
	case CODE:			\
		t_op = TOP;		\
		f_op = FOP;		\
		goto cond_branch

			COND_SEL(BPF_JMP | BPF_JGT | BPF_K, BGU, BLEU);
			COND_SEL(BPF_JMP | BPF_JGE | BPF_K, BGEU, BLU);
			COND_SEL(BPF_JMP | BPF_JEQ | BPF_K, BE, BNE);
			COND_SEL(BPF_JMP | BPF_JSET | BPF_K, BNE, BE);
			COND_SEL(BPF_JMP | BPF_JGT | BPF_X, BGU, BLEU);
			COND_SEL(BPF_JMP | BPF_JGE | BPF_X, BGEU, BLU);
			COND_SEL(BPF_JMP | BPF_JEQ | BPF_X, BE, BNE);
			COND_SEL(BPF_JMP | BPF_JSET | BPF_X, BNE, BE);

cond_branch:			f_offset = addrs[i + filter[i].jf];
				t_offset = addrs[i + filter[i].jt];

				/* same targets, can avoid doing the test :) */
				if (filter[i].jt == filter[i].jf) {
					emit_jump(t_offset);
					emit_nop();
					break;
				}

				switch (code) {
				case BPF_JMP | BPF_JGT | BPF_X:
				case BPF_JMP | BPF_JGE | BPF_X:
				case BPF_JMP | BPF_JEQ | BPF_X:
					seen |= SEEN_XREG;
					emit_cmp(r_A, r_X);
					break;
				case BPF_JMP | BPF_JSET | BPF_X:
					seen |= SEEN_XREG;
					emit_btst(r_A, r_X);
					break;
				case BPF_JMP | BPF_JEQ | BPF_K:
				case BPF_JMP | BPF_JGT | BPF_K:
				case BPF_JMP | BPF_JGE | BPF_K:
					if (is_simm13(K)) {
						emit_cmpi(r_A, K);
					} else {
						emit_loadimm(K, r_TMP);
						emit_cmp(r_A, r_TMP);
					}
					break;
				case BPF_JMP | BPF_JSET | BPF_K:
					if (is_simm13(K)) {
						emit_btsti(r_A, K);
					} else {
						emit_loadimm(K, r_TMP);
						emit_btst(r_A, r_TMP);
					}
					break;
				}
				if (filter[i].jt != 0) {
					if (filter[i].jf)
						t_offset += 8;
					emit_branch(t_op, t_offset);
					emit_nop(); /* delay slot */
					if (filter[i].jf) {
						emit_jump(f_offset);
						emit_nop();
					}
					break;
				}
				emit_branch(f_op, f_offset);
				emit_nop(); /* delay slot */
				break;

			default:
				/* hmm, too complex filter, give up with jit compiler */
				goto out;
			}
			ilen = (void *) prog - (void *) temp;
			if (image) {
				if (unlikely(proglen + ilen > oldproglen)) {
					pr_err("bpb_jit_compile fatal error\n");
					kfree(addrs);
					module_free(NULL, image);
					return;
				}
				memcpy(image + proglen, temp, ilen);
			}
			proglen += ilen;
			addrs[i] = proglen;
			prog = temp;
		}
		/* last bpf instruction is always a RET :
		 * use it to give the cleanup instruction(s) addr
		 */
		cleanup_addr = proglen - 8; /* jmpl; mov r_A,%o0; */
		if (seen_or_pass0 & SEEN_MEM)
			cleanup_addr -= 4; /* add %sp, X, %sp; */

		if (image) {
			if (proglen != oldproglen)
				pr_err("bpb_jit_compile proglen=%u != oldproglen=%u\n",
				       proglen, oldproglen);
			break;
		}
		if (proglen == oldproglen) {
			image = module_alloc(proglen);
			if (!image)
				goto out;
		}
		oldproglen = proglen;
	}

	if (bpf_jit_enable > 1)
		bpf_jit_dump(flen, proglen, pass, image);

	if (image) {
		bpf_flush_icache(image, image + proglen);
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
