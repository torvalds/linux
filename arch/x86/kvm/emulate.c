/******************************************************************************
 * emulate.c
 *
 * Generic x86 (32-bit and 64-bit) instruction decoder and emulator.
 *
 * Copyright (c) 2005 Keir Fraser
 *
 * Linux coding style, mod r/m decoder, segment base fixes, real-mode
 * privileged instructions:
 *
 * Copyright (C) 2006 Qumranet
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 *
 *   Avi Kivity <avi@qumranet.com>
 *   Yaniv Kamay <yaniv@qumranet.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * From: xen-unstable 10676:af9809f51f81a3c43f276f00c81a52ef558afda4
 */

#include <linux/kvm_host.h>
#include "kvm_cache_regs.h"
#include <linux/module.h>
#include <asm/kvm_emulate.h>

#include "x86.h"
#include "tss.h"

/*
 * Opcode effective-address decode tables.
 * Note that we only emulate instructions that have at least one memory
 * operand (excluding implicit stack references). We assume that stack
 * references and instruction fetches will never occur in special memory
 * areas that require emulation. So, for example, 'mov <imm>,<reg>' need
 * not be handled.
 */

/* Operand sizes: 8-bit operands or specified/overridden size. */
#define ByteOp      (1<<0)	/* 8-bit operands. */
/* Destination operand type. */
#define ImplicitOps (1<<1)	/* Implicit in opcode. No generic decode. */
#define DstReg      (2<<1)	/* Register operand. */
#define DstMem      (3<<1)	/* Memory operand. */
#define DstAcc      (4<<1)	/* Destination Accumulator */
#define DstDI       (5<<1)	/* Destination is in ES:(E)DI */
#define DstMem64    (6<<1)	/* 64bit memory operand */
#define DstImmUByte (7<<1)	/* 8-bit unsigned immediate operand */
#define DstDX       (8<<1)	/* Destination is in DX register */
#define DstMask     (0xf<<1)
/* Source operand type. */
#define SrcNone     (0<<5)	/* No source operand. */
#define SrcReg      (1<<5)	/* Register operand. */
#define SrcMem      (2<<5)	/* Memory operand. */
#define SrcMem16    (3<<5)	/* Memory operand (16-bit). */
#define SrcMem32    (4<<5)	/* Memory operand (32-bit). */
#define SrcImm      (5<<5)	/* Immediate operand. */
#define SrcImmByte  (6<<5)	/* 8-bit sign-extended immediate operand. */
#define SrcOne      (7<<5)	/* Implied '1' */
#define SrcImmUByte (8<<5)      /* 8-bit unsigned immediate operand. */
#define SrcImmU     (9<<5)      /* Immediate operand, unsigned */
#define SrcSI       (0xa<<5)	/* Source is in the DS:RSI */
#define SrcImmFAddr (0xb<<5)	/* Source is immediate far address */
#define SrcMemFAddr (0xc<<5)	/* Source is far address in memory */
#define SrcAcc      (0xd<<5)	/* Source Accumulator */
#define SrcImmU16   (0xe<<5)    /* Immediate operand, unsigned, 16 bits */
#define SrcDX       (0xf<<5)	/* Source is in DX register */
#define SrcMask     (0xf<<5)
/* Generic ModRM decode. */
#define ModRM       (1<<9)
/* Destination is only written; never read. */
#define Mov         (1<<10)
#define BitOp       (1<<11)
#define MemAbs      (1<<12)      /* Memory operand is absolute displacement */
#define String      (1<<13)     /* String instruction (rep capable) */
#define Stack       (1<<14)     /* Stack instruction (push/pop) */
#define GroupMask   (7<<15)     /* Opcode uses one of the group mechanisms */
#define Group       (1<<15)     /* Bits 3:5 of modrm byte extend opcode */
#define GroupDual   (2<<15)     /* Alternate decoding of mod == 3 */
#define Prefix      (3<<15)     /* Instruction varies with 66/f2/f3 prefix */
#define RMExt       (4<<15)     /* Opcode extension in ModRM r/m if mod == 3 */
#define Sse         (1<<18)     /* SSE Vector instruction */
/* Misc flags */
#define Prot        (1<<21) /* instruction generates #UD if not in prot-mode */
#define VendorSpecific (1<<22) /* Vendor specific instruction */
#define NoAccess    (1<<23) /* Don't access memory (lea/invlpg/verr etc) */
#define Op3264      (1<<24) /* Operand is 64b in long mode, 32b otherwise */
#define Undefined   (1<<25) /* No Such Instruction */
#define Lock        (1<<26) /* lock prefix is allowed for the instruction */
#define Priv        (1<<27) /* instruction generates #GP if current CPL != 0 */
#define No64	    (1<<28)
/* Source 2 operand type */
#define Src2None    (0<<29)
#define Src2CL      (1<<29)
#define Src2ImmByte (2<<29)
#define Src2One     (3<<29)
#define Src2Imm     (4<<29)
#define Src2Mask    (7<<29)

#define X2(x...) x, x
#define X3(x...) X2(x), x
#define X4(x...) X2(x), X2(x)
#define X5(x...) X4(x), x
#define X6(x...) X4(x), X2(x)
#define X7(x...) X4(x), X3(x)
#define X8(x...) X4(x), X4(x)
#define X16(x...) X8(x), X8(x)

struct opcode {
	u32 flags;
	u8 intercept;
	union {
		int (*execute)(struct x86_emulate_ctxt *ctxt);
		struct opcode *group;
		struct group_dual *gdual;
		struct gprefix *gprefix;
	} u;
	int (*check_perm)(struct x86_emulate_ctxt *ctxt);
};

struct group_dual {
	struct opcode mod012[8];
	struct opcode mod3[8];
};

struct gprefix {
	struct opcode pfx_no;
	struct opcode pfx_66;
	struct opcode pfx_f2;
	struct opcode pfx_f3;
};

/* EFLAGS bit definitions. */
#define EFLG_ID (1<<21)
#define EFLG_VIP (1<<20)
#define EFLG_VIF (1<<19)
#define EFLG_AC (1<<18)
#define EFLG_VM (1<<17)
#define EFLG_RF (1<<16)
#define EFLG_IOPL (3<<12)
#define EFLG_NT (1<<14)
#define EFLG_OF (1<<11)
#define EFLG_DF (1<<10)
#define EFLG_IF (1<<9)
#define EFLG_TF (1<<8)
#define EFLG_SF (1<<7)
#define EFLG_ZF (1<<6)
#define EFLG_AF (1<<4)
#define EFLG_PF (1<<2)
#define EFLG_CF (1<<0)

#define EFLG_RESERVED_ZEROS_MASK 0xffc0802a
#define EFLG_RESERVED_ONE_MASK 2

/*
 * Instruction emulation:
 * Most instructions are emulated directly via a fragment of inline assembly
 * code. This allows us to save/restore EFLAGS and thus very easily pick up
 * any modified flags.
 */

#if defined(CONFIG_X86_64)
#define _LO32 "k"		/* force 32-bit operand */
#define _STK  "%%rsp"		/* stack pointer */
#elif defined(__i386__)
#define _LO32 ""		/* force 32-bit operand */
#define _STK  "%%esp"		/* stack pointer */
#endif

/*
 * These EFLAGS bits are restored from saved value during emulation, and
 * any changes are written back to the saved value after emulation.
 */
#define EFLAGS_MASK (EFLG_OF|EFLG_SF|EFLG_ZF|EFLG_AF|EFLG_PF|EFLG_CF)

/* Before executing instruction: restore necessary bits in EFLAGS. */
#define _PRE_EFLAGS(_sav, _msk, _tmp)					\
	/* EFLAGS = (_sav & _msk) | (EFLAGS & ~_msk); _sav &= ~_msk; */ \
	"movl %"_sav",%"_LO32 _tmp"; "                                  \
	"push %"_tmp"; "                                                \
	"push %"_tmp"; "                                                \
	"movl %"_msk",%"_LO32 _tmp"; "                                  \
	"andl %"_LO32 _tmp",("_STK"); "                                 \
	"pushf; "                                                       \
	"notl %"_LO32 _tmp"; "                                          \
	"andl %"_LO32 _tmp",("_STK"); "                                 \
	"andl %"_LO32 _tmp","__stringify(BITS_PER_LONG/4)"("_STK"); "	\
	"pop  %"_tmp"; "                                                \
	"orl  %"_LO32 _tmp",("_STK"); "                                 \
	"popf; "                                                        \
	"pop  %"_sav"; "

/* After executing instruction: write-back necessary bits in EFLAGS. */
#define _POST_EFLAGS(_sav, _msk, _tmp) \
	/* _sav |= EFLAGS & _msk; */		\
	"pushf; "				\
	"pop  %"_tmp"; "			\
	"andl %"_msk",%"_LO32 _tmp"; "		\
	"orl  %"_LO32 _tmp",%"_sav"; "

#ifdef CONFIG_X86_64
#define ON64(x) x
#else
#define ON64(x)
#endif

#define ____emulate_2op(_op, _src, _dst, _eflags, _x, _y, _suffix, _dsttype) \
	do {								\
		__asm__ __volatile__ (					\
			_PRE_EFLAGS("0", "4", "2")			\
			_op _suffix " %"_x"3,%1; "			\
			_POST_EFLAGS("0", "4", "2")			\
			: "=m" (_eflags), "+q" (*(_dsttype*)&(_dst).val),\
			  "=&r" (_tmp)					\
			: _y ((_src).val), "i" (EFLAGS_MASK));		\
	} while (0)


/* Raw emulation: instruction has two explicit operands. */
#define __emulate_2op_nobyte(_op,_src,_dst,_eflags,_wx,_wy,_lx,_ly,_qx,_qy) \
	do {								\
		unsigned long _tmp;					\
									\
		switch ((_dst).bytes) {					\
		case 2:							\
			____emulate_2op(_op,_src,_dst,_eflags,_wx,_wy,"w",u16);\
			break;						\
		case 4:							\
			____emulate_2op(_op,_src,_dst,_eflags,_lx,_ly,"l",u32);\
			break;						\
		case 8:							\
			ON64(____emulate_2op(_op,_src,_dst,_eflags,_qx,_qy,"q",u64)); \
			break;						\
		}							\
	} while (0)

#define __emulate_2op(_op,_src,_dst,_eflags,_bx,_by,_wx,_wy,_lx,_ly,_qx,_qy) \
	do {								     \
		unsigned long _tmp;					     \
		switch ((_dst).bytes) {				             \
		case 1:							     \
			____emulate_2op(_op,_src,_dst,_eflags,_bx,_by,"b",u8); \
			break;						     \
		default:						     \
			__emulate_2op_nobyte(_op, _src, _dst, _eflags,	     \
					     _wx, _wy, _lx, _ly, _qx, _qy);  \
			break;						     \
		}							     \
	} while (0)

/* Source operand is byte-sized and may be restricted to just %cl. */
#define emulate_2op_SrcB(_op, _src, _dst, _eflags)                      \
	__emulate_2op(_op, _src, _dst, _eflags,				\
		      "b", "c", "b", "c", "b", "c", "b", "c")

/* Source operand is byte, word, long or quad sized. */
#define emulate_2op_SrcV(_op, _src, _dst, _eflags)                      \
	__emulate_2op(_op, _src, _dst, _eflags,				\
		      "b", "q", "w", "r", _LO32, "r", "", "r")

/* Source operand is word, long or quad sized. */
#define emulate_2op_SrcV_nobyte(_op, _src, _dst, _eflags)               \
	__emulate_2op_nobyte(_op, _src, _dst, _eflags,			\
			     "w", "r", _LO32, "r", "", "r")

/* Instruction has three operands and one operand is stored in ECX register */
#define __emulate_2op_cl(_op, _cl, _src, _dst, _eflags, _suffix, _type)	\
	do {								\
		unsigned long _tmp;					\
		_type _clv  = (_cl).val;				\
		_type _srcv = (_src).val;				\
		_type _dstv = (_dst).val;				\
									\
		__asm__ __volatile__ (					\
			_PRE_EFLAGS("0", "5", "2")			\
			_op _suffix " %4,%1 \n"				\
			_POST_EFLAGS("0", "5", "2")			\
			: "=m" (_eflags), "+r" (_dstv), "=&r" (_tmp)	\
			: "c" (_clv) , "r" (_srcv), "i" (EFLAGS_MASK)	\
			);						\
									\
		(_cl).val  = (unsigned long) _clv;			\
		(_src).val = (unsigned long) _srcv;			\
		(_dst).val = (unsigned long) _dstv;			\
	} while (0)

#define emulate_2op_cl(_op, _cl, _src, _dst, _eflags)			\
	do {								\
		switch ((_dst).bytes) {					\
		case 2:							\
			__emulate_2op_cl(_op, _cl, _src, _dst, _eflags,	\
					 "w", unsigned short);         	\
			break;						\
		case 4:							\
			__emulate_2op_cl(_op, _cl, _src, _dst, _eflags,	\
					 "l", unsigned int);           	\
			break;						\
		case 8:							\
			ON64(__emulate_2op_cl(_op, _cl, _src, _dst, _eflags, \
					      "q", unsigned long));	\
			break;						\
		}							\
	} while (0)

#define __emulate_1op(_op, _dst, _eflags, _suffix)			\
	do {								\
		unsigned long _tmp;					\
									\
		__asm__ __volatile__ (					\
			_PRE_EFLAGS("0", "3", "2")			\
			_op _suffix " %1; "				\
			_POST_EFLAGS("0", "3", "2")			\
			: "=m" (_eflags), "+m" ((_dst).val),		\
			  "=&r" (_tmp)					\
			: "i" (EFLAGS_MASK));				\
	} while (0)

/* Instruction has only one explicit operand (no source operand). */
#define emulate_1op(_op, _dst, _eflags)                                    \
	do {								\
		switch ((_dst).bytes) {				        \
		case 1:	__emulate_1op(_op, _dst, _eflags, "b"); break;	\
		case 2:	__emulate_1op(_op, _dst, _eflags, "w"); break;	\
		case 4:	__emulate_1op(_op, _dst, _eflags, "l"); break;	\
		case 8:	ON64(__emulate_1op(_op, _dst, _eflags, "q")); break; \
		}							\
	} while (0)

#define __emulate_1op_rax_rdx(_op, _src, _rax, _rdx, _eflags, _suffix)		\
	do {								\
		unsigned long _tmp;					\
									\
		__asm__ __volatile__ (					\
			_PRE_EFLAGS("0", "4", "1")			\
			_op _suffix " %5; "				\
			_POST_EFLAGS("0", "4", "1")			\
			: "=m" (_eflags), "=&r" (_tmp),			\
			  "+a" (_rax), "+d" (_rdx)			\
			: "i" (EFLAGS_MASK), "m" ((_src).val),		\
			  "a" (_rax), "d" (_rdx));			\
	} while (0)

#define __emulate_1op_rax_rdx_ex(_op, _src, _rax, _rdx, _eflags, _suffix, _ex) \
	do {								\
		unsigned long _tmp;					\
									\
		__asm__ __volatile__ (					\
			_PRE_EFLAGS("0", "5", "1")			\
			"1: \n\t"					\
			_op _suffix " %6; "				\
			"2: \n\t"					\
			_POST_EFLAGS("0", "5", "1")			\
			".pushsection .fixup,\"ax\" \n\t"		\
			"3: movb $1, %4 \n\t"				\
			"jmp 2b \n\t"					\
			".popsection \n\t"				\
			_ASM_EXTABLE(1b, 3b)				\
			: "=m" (_eflags), "=&r" (_tmp),			\
			  "+a" (_rax), "+d" (_rdx), "+qm"(_ex)		\
			: "i" (EFLAGS_MASK), "m" ((_src).val),		\
			  "a" (_rax), "d" (_rdx));			\
	} while (0)

/* instruction has only one source operand, destination is implicit (e.g. mul, div, imul, idiv) */
#define emulate_1op_rax_rdx(_op, _src, _rax, _rdx, _eflags)		\
	do {								\
		switch((_src).bytes) {					\
		case 1:							\
			__emulate_1op_rax_rdx(_op, _src, _rax, _rdx,	\
					      _eflags, "b");		\
			break;						\
		case 2:							\
			__emulate_1op_rax_rdx(_op, _src, _rax, _rdx,	\
					      _eflags, "w");		\
			break;						\
		case 4:							\
			__emulate_1op_rax_rdx(_op, _src, _rax, _rdx,	\
					      _eflags, "l");		\
			break;						\
		case 8:							\
			ON64(__emulate_1op_rax_rdx(_op, _src, _rax, _rdx, \
						   _eflags, "q"));	\
			break;						\
		}							\
	} while (0)

#define emulate_1op_rax_rdx_ex(_op, _src, _rax, _rdx, _eflags, _ex)	\
	do {								\
		switch((_src).bytes) {					\
		case 1:							\
			__emulate_1op_rax_rdx_ex(_op, _src, _rax, _rdx,	\
						 _eflags, "b", _ex);	\
			break;						\
		case 2:							\
			__emulate_1op_rax_rdx_ex(_op, _src, _rax, _rdx, \
						 _eflags, "w", _ex);	\
			break;						\
		case 4:							\
			__emulate_1op_rax_rdx_ex(_op, _src, _rax, _rdx, \
						 _eflags, "l", _ex);	\
			break;						\
		case 8: ON64(						\
			__emulate_1op_rax_rdx_ex(_op, _src, _rax, _rdx, \
						 _eflags, "q", _ex));	\
			break;						\
		}							\
	} while (0)

/* Fetch next part of the instruction being emulated. */
#define insn_fetch(_type, _size, _eip)                                  \
({	unsigned long _x;						\
	rc = do_insn_fetch(ctxt, ops, (_eip), &_x, (_size));		\
	if (rc != X86EMUL_CONTINUE)					\
		goto done;						\
	(_eip) += (_size);						\
	(_type)_x;							\
})

#define insn_fetch_arr(_arr, _size, _eip)				\
({	rc = do_insn_fetch(ctxt, ops, (_eip), _arr, (_size));		\
	if (rc != X86EMUL_CONTINUE)					\
		goto done;						\
	(_eip) += (_size);						\
})

static int emulator_check_intercept(struct x86_emulate_ctxt *ctxt,
				    enum x86_intercept intercept,
				    enum x86_intercept_stage stage)
{
	struct x86_instruction_info info = {
		.intercept  = intercept,
		.rep_prefix = ctxt->decode.rep_prefix,
		.modrm_mod  = ctxt->decode.modrm_mod,
		.modrm_reg  = ctxt->decode.modrm_reg,
		.modrm_rm   = ctxt->decode.modrm_rm,
		.src_val    = ctxt->decode.src.val64,
		.src_bytes  = ctxt->decode.src.bytes,
		.dst_bytes  = ctxt->decode.dst.bytes,
		.ad_bytes   = ctxt->decode.ad_bytes,
		.next_rip   = ctxt->eip,
	};

	return ctxt->ops->intercept(ctxt, &info, stage);
}

static inline unsigned long ad_mask(struct decode_cache *c)
{
	return (1UL << (c->ad_bytes << 3)) - 1;
}

/* Access/update address held in a register, based on addressing mode. */
static inline unsigned long
address_mask(struct decode_cache *c, unsigned long reg)
{
	if (c->ad_bytes == sizeof(unsigned long))
		return reg;
	else
		return reg & ad_mask(c);
}

static inline unsigned long
register_address(struct decode_cache *c, unsigned long reg)
{
	return address_mask(c, reg);
}

static inline void
register_address_increment(struct decode_cache *c, unsigned long *reg, int inc)
{
	if (c->ad_bytes == sizeof(unsigned long))
		*reg += inc;
	else
		*reg = (*reg & ~ad_mask(c)) | ((*reg + inc) & ad_mask(c));
}

static inline void jmp_rel(struct decode_cache *c, int rel)
{
	register_address_increment(c, &c->eip, rel);
}

static u32 desc_limit_scaled(struct desc_struct *desc)
{
	u32 limit = get_desc_limit(desc);

	return desc->g ? (limit << 12) | 0xfff : limit;
}

static void set_seg_override(struct decode_cache *c, int seg)
{
	c->has_seg_override = true;
	c->seg_override = seg;
}

static unsigned long seg_base(struct x86_emulate_ctxt *ctxt,
			      struct x86_emulate_ops *ops, int seg)
{
	if (ctxt->mode == X86EMUL_MODE_PROT64 && seg < VCPU_SREG_FS)
		return 0;

	return ops->get_cached_segment_base(ctxt, seg);
}

static unsigned seg_override(struct x86_emulate_ctxt *ctxt,
			     struct decode_cache *c)
{
	if (!c->has_seg_override)
		return 0;

	return c->seg_override;
}

static int emulate_exception(struct x86_emulate_ctxt *ctxt, int vec,
			     u32 error, bool valid)
{
	ctxt->exception.vector = vec;
	ctxt->exception.error_code = error;
	ctxt->exception.error_code_valid = valid;
	return X86EMUL_PROPAGATE_FAULT;
}

static int emulate_db(struct x86_emulate_ctxt *ctxt)
{
	return emulate_exception(ctxt, DB_VECTOR, 0, false);
}

static int emulate_gp(struct x86_emulate_ctxt *ctxt, int err)
{
	return emulate_exception(ctxt, GP_VECTOR, err, true);
}

static int emulate_ss(struct x86_emulate_ctxt *ctxt, int err)
{
	return emulate_exception(ctxt, SS_VECTOR, err, true);
}

static int emulate_ud(struct x86_emulate_ctxt *ctxt)
{
	return emulate_exception(ctxt, UD_VECTOR, 0, false);
}

static int emulate_ts(struct x86_emulate_ctxt *ctxt, int err)
{
	return emulate_exception(ctxt, TS_VECTOR, err, true);
}

static int emulate_de(struct x86_emulate_ctxt *ctxt)
{
	return emulate_exception(ctxt, DE_VECTOR, 0, false);
}

static int emulate_nm(struct x86_emulate_ctxt *ctxt)
{
	return emulate_exception(ctxt, NM_VECTOR, 0, false);
}

static u16 get_segment_selector(struct x86_emulate_ctxt *ctxt, unsigned seg)
{
	u16 selector;
	struct desc_struct desc;

	ctxt->ops->get_segment(ctxt, &selector, &desc, NULL, seg);
	return selector;
}

static void set_segment_selector(struct x86_emulate_ctxt *ctxt, u16 selector,
				 unsigned seg)
{
	u16 dummy;
	u32 base3;
	struct desc_struct desc;

	ctxt->ops->get_segment(ctxt, &dummy, &desc, &base3, seg);
	ctxt->ops->set_segment(ctxt, selector, &desc, base3, seg);
}

static int __linearize(struct x86_emulate_ctxt *ctxt,
		     struct segmented_address addr,
		     unsigned size, bool write, bool fetch,
		     ulong *linear)
{
	struct decode_cache *c = &ctxt->decode;
	struct desc_struct desc;
	bool usable;
	ulong la;
	u32 lim;
	u16 sel;
	unsigned cpl, rpl;

	la = seg_base(ctxt, ctxt->ops, addr.seg) + addr.ea;
	switch (ctxt->mode) {
	case X86EMUL_MODE_REAL:
		break;
	case X86EMUL_MODE_PROT64:
		if (((signed long)la << 16) >> 16 != la)
			return emulate_gp(ctxt, 0);
		break;
	default:
		usable = ctxt->ops->get_segment(ctxt, &sel, &desc, NULL,
						addr.seg);
		if (!usable)
			goto bad;
		/* code segment or read-only data segment */
		if (((desc.type & 8) || !(desc.type & 2)) && write)
			goto bad;
		/* unreadable code segment */
		if (!fetch && (desc.type & 8) && !(desc.type & 2))
			goto bad;
		lim = desc_limit_scaled(&desc);
		if ((desc.type & 8) || !(desc.type & 4)) {
			/* expand-up segment */
			if (addr.ea > lim || (u32)(addr.ea + size - 1) > lim)
				goto bad;
		} else {
			/* exapand-down segment */
			if (addr.ea <= lim || (u32)(addr.ea + size - 1) <= lim)
				goto bad;
			lim = desc.d ? 0xffffffff : 0xffff;
			if (addr.ea > lim || (u32)(addr.ea + size - 1) > lim)
				goto bad;
		}
		cpl = ctxt->ops->cpl(ctxt);
		rpl = sel & 3;
		cpl = max(cpl, rpl);
		if (!(desc.type & 8)) {
			/* data segment */
			if (cpl > desc.dpl)
				goto bad;
		} else if ((desc.type & 8) && !(desc.type & 4)) {
			/* nonconforming code segment */
			if (cpl != desc.dpl)
				goto bad;
		} else if ((desc.type & 8) && (desc.type & 4)) {
			/* conforming code segment */
			if (cpl < desc.dpl)
				goto bad;
		}
		break;
	}
	if (fetch ? ctxt->mode != X86EMUL_MODE_PROT64 : c->ad_bytes != 8)
		la &= (u32)-1;
	*linear = la;
	return X86EMUL_CONTINUE;
bad:
	if (addr.seg == VCPU_SREG_SS)
		return emulate_ss(ctxt, addr.seg);
	else
		return emulate_gp(ctxt, addr.seg);
}

static int linearize(struct x86_emulate_ctxt *ctxt,
		     struct segmented_address addr,
		     unsigned size, bool write,
		     ulong *linear)
{
	return __linearize(ctxt, addr, size, write, false, linear);
}


static int segmented_read_std(struct x86_emulate_ctxt *ctxt,
			      struct segmented_address addr,
			      void *data,
			      unsigned size)
{
	int rc;
	ulong linear;

	rc = linearize(ctxt, addr, size, false, &linear);
	if (rc != X86EMUL_CONTINUE)
		return rc;
	return ctxt->ops->read_std(ctxt, linear, data, size, &ctxt->exception);
}

static int do_fetch_insn_byte(struct x86_emulate_ctxt *ctxt,
			      struct x86_emulate_ops *ops,
			      unsigned long eip, u8 *dest)
{
	struct fetch_cache *fc = &ctxt->decode.fetch;
	int rc;
	int size, cur_size;

	if (eip == fc->end) {
		unsigned long linear;
		struct segmented_address addr = { .seg=VCPU_SREG_CS, .ea=eip};
		cur_size = fc->end - fc->start;
		size = min(15UL - cur_size, PAGE_SIZE - offset_in_page(eip));
		rc = __linearize(ctxt, addr, size, false, true, &linear);
		if (rc != X86EMUL_CONTINUE)
			return rc;
		rc = ops->fetch(ctxt, linear, fc->data + cur_size,
				size, &ctxt->exception);
		if (rc != X86EMUL_CONTINUE)
			return rc;
		fc->end += size;
	}
	*dest = fc->data[eip - fc->start];
	return X86EMUL_CONTINUE;
}

static int do_insn_fetch(struct x86_emulate_ctxt *ctxt,
			 struct x86_emulate_ops *ops,
			 unsigned long eip, void *dest, unsigned size)
{
	int rc;

	/* x86 instructions are limited to 15 bytes. */
	if (eip + size - ctxt->eip > 15)
		return X86EMUL_UNHANDLEABLE;
	while (size--) {
		rc = do_fetch_insn_byte(ctxt, ops, eip++, dest++);
		if (rc != X86EMUL_CONTINUE)
			return rc;
	}
	return X86EMUL_CONTINUE;
}

/*
 * Given the 'reg' portion of a ModRM byte, and a register block, return a
 * pointer into the block that addresses the relevant register.
 * @highbyte_regs specifies whether to decode AH,CH,DH,BH.
 */
static void *decode_register(u8 modrm_reg, unsigned long *regs,
			     int highbyte_regs)
{
	void *p;

	p = &regs[modrm_reg];
	if (highbyte_regs && modrm_reg >= 4 && modrm_reg < 8)
		p = (unsigned char *)&regs[modrm_reg & 3] + 1;
	return p;
}

static int read_descriptor(struct x86_emulate_ctxt *ctxt,
			   struct segmented_address addr,
			   u16 *size, unsigned long *address, int op_bytes)
{
	int rc;

	if (op_bytes == 2)
		op_bytes = 3;
	*address = 0;
	rc = segmented_read_std(ctxt, addr, size, 2);
	if (rc != X86EMUL_CONTINUE)
		return rc;
	addr.ea += 2;
	rc = segmented_read_std(ctxt, addr, address, op_bytes);
	return rc;
}

static int test_cc(unsigned int condition, unsigned int flags)
{
	int rc = 0;

	switch ((condition & 15) >> 1) {
	case 0: /* o */
		rc |= (flags & EFLG_OF);
		break;
	case 1: /* b/c/nae */
		rc |= (flags & EFLG_CF);
		break;
	case 2: /* z/e */
		rc |= (flags & EFLG_ZF);
		break;
	case 3: /* be/na */
		rc |= (flags & (EFLG_CF|EFLG_ZF));
		break;
	case 4: /* s */
		rc |= (flags & EFLG_SF);
		break;
	case 5: /* p/pe */
		rc |= (flags & EFLG_PF);
		break;
	case 7: /* le/ng */
		rc |= (flags & EFLG_ZF);
		/* fall through */
	case 6: /* l/nge */
		rc |= (!(flags & EFLG_SF) != !(flags & EFLG_OF));
		break;
	}

	/* Odd condition identifiers (lsb == 1) have inverted sense. */
	return (!!rc ^ (condition & 1));
}

static void fetch_register_operand(struct operand *op)
{
	switch (op->bytes) {
	case 1:
		op->val = *(u8 *)op->addr.reg;
		break;
	case 2:
		op->val = *(u16 *)op->addr.reg;
		break;
	case 4:
		op->val = *(u32 *)op->addr.reg;
		break;
	case 8:
		op->val = *(u64 *)op->addr.reg;
		break;
	}
}

static void read_sse_reg(struct x86_emulate_ctxt *ctxt, sse128_t *data, int reg)
{
	ctxt->ops->get_fpu(ctxt);
	switch (reg) {
	case 0: asm("movdqu %%xmm0, %0" : "=m"(*data)); break;
	case 1: asm("movdqu %%xmm1, %0" : "=m"(*data)); break;
	case 2: asm("movdqu %%xmm2, %0" : "=m"(*data)); break;
	case 3: asm("movdqu %%xmm3, %0" : "=m"(*data)); break;
	case 4: asm("movdqu %%xmm4, %0" : "=m"(*data)); break;
	case 5: asm("movdqu %%xmm5, %0" : "=m"(*data)); break;
	case 6: asm("movdqu %%xmm6, %0" : "=m"(*data)); break;
	case 7: asm("movdqu %%xmm7, %0" : "=m"(*data)); break;
#ifdef CONFIG_X86_64
	case 8: asm("movdqu %%xmm8, %0" : "=m"(*data)); break;
	case 9: asm("movdqu %%xmm9, %0" : "=m"(*data)); break;
	case 10: asm("movdqu %%xmm10, %0" : "=m"(*data)); break;
	case 11: asm("movdqu %%xmm11, %0" : "=m"(*data)); break;
	case 12: asm("movdqu %%xmm12, %0" : "=m"(*data)); break;
	case 13: asm("movdqu %%xmm13, %0" : "=m"(*data)); break;
	case 14: asm("movdqu %%xmm14, %0" : "=m"(*data)); break;
	case 15: asm("movdqu %%xmm15, %0" : "=m"(*data)); break;
#endif
	default: BUG();
	}
	ctxt->ops->put_fpu(ctxt);
}

static void write_sse_reg(struct x86_emulate_ctxt *ctxt, sse128_t *data,
			  int reg)
{
	ctxt->ops->get_fpu(ctxt);
	switch (reg) {
	case 0: asm("movdqu %0, %%xmm0" : : "m"(*data)); break;
	case 1: asm("movdqu %0, %%xmm1" : : "m"(*data)); break;
	case 2: asm("movdqu %0, %%xmm2" : : "m"(*data)); break;
	case 3: asm("movdqu %0, %%xmm3" : : "m"(*data)); break;
	case 4: asm("movdqu %0, %%xmm4" : : "m"(*data)); break;
	case 5: asm("movdqu %0, %%xmm5" : : "m"(*data)); break;
	case 6: asm("movdqu %0, %%xmm6" : : "m"(*data)); break;
	case 7: asm("movdqu %0, %%xmm7" : : "m"(*data)); break;
#ifdef CONFIG_X86_64
	case 8: asm("movdqu %0, %%xmm8" : : "m"(*data)); break;
	case 9: asm("movdqu %0, %%xmm9" : : "m"(*data)); break;
	case 10: asm("movdqu %0, %%xmm10" : : "m"(*data)); break;
	case 11: asm("movdqu %0, %%xmm11" : : "m"(*data)); break;
	case 12: asm("movdqu %0, %%xmm12" : : "m"(*data)); break;
	case 13: asm("movdqu %0, %%xmm13" : : "m"(*data)); break;
	case 14: asm("movdqu %0, %%xmm14" : : "m"(*data)); break;
	case 15: asm("movdqu %0, %%xmm15" : : "m"(*data)); break;
#endif
	default: BUG();
	}
	ctxt->ops->put_fpu(ctxt);
}

static void decode_register_operand(struct x86_emulate_ctxt *ctxt,
				    struct operand *op,
				    struct decode_cache *c,
				    int inhibit_bytereg)
{
	unsigned reg = c->modrm_reg;
	int highbyte_regs = c->rex_prefix == 0;

	if (!(c->d & ModRM))
		reg = (c->b & 7) | ((c->rex_prefix & 1) << 3);

	if (c->d & Sse) {
		op->type = OP_XMM;
		op->bytes = 16;
		op->addr.xmm = reg;
		read_sse_reg(ctxt, &op->vec_val, reg);
		return;
	}

	op->type = OP_REG;
	if ((c->d & ByteOp) && !inhibit_bytereg) {
		op->addr.reg = decode_register(reg, c->regs, highbyte_regs);
		op->bytes = 1;
	} else {
		op->addr.reg = decode_register(reg, c->regs, 0);
		op->bytes = c->op_bytes;
	}
	fetch_register_operand(op);
	op->orig_val = op->val;
}

static int decode_modrm(struct x86_emulate_ctxt *ctxt,
			struct x86_emulate_ops *ops,
			struct operand *op)
{
	struct decode_cache *c = &ctxt->decode;
	u8 sib;
	int index_reg = 0, base_reg = 0, scale;
	int rc = X86EMUL_CONTINUE;
	ulong modrm_ea = 0;

	if (c->rex_prefix) {
		c->modrm_reg = (c->rex_prefix & 4) << 1;	/* REX.R */
		index_reg = (c->rex_prefix & 2) << 2; /* REX.X */
		c->modrm_rm = base_reg = (c->rex_prefix & 1) << 3; /* REG.B */
	}

	c->modrm = insn_fetch(u8, 1, c->eip);
	c->modrm_mod |= (c->modrm & 0xc0) >> 6;
	c->modrm_reg |= (c->modrm & 0x38) >> 3;
	c->modrm_rm |= (c->modrm & 0x07);
	c->modrm_seg = VCPU_SREG_DS;

	if (c->modrm_mod == 3) {
		op->type = OP_REG;
		op->bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		op->addr.reg = decode_register(c->modrm_rm,
					       c->regs, c->d & ByteOp);
		if (c->d & Sse) {
			op->type = OP_XMM;
			op->bytes = 16;
			op->addr.xmm = c->modrm_rm;
			read_sse_reg(ctxt, &op->vec_val, c->modrm_rm);
			return rc;
		}
		fetch_register_operand(op);
		return rc;
	}

	op->type = OP_MEM;

	if (c->ad_bytes == 2) {
		unsigned bx = c->regs[VCPU_REGS_RBX];
		unsigned bp = c->regs[VCPU_REGS_RBP];
		unsigned si = c->regs[VCPU_REGS_RSI];
		unsigned di = c->regs[VCPU_REGS_RDI];

		/* 16-bit ModR/M decode. */
		switch (c->modrm_mod) {
		case 0:
			if (c->modrm_rm == 6)
				modrm_ea += insn_fetch(u16, 2, c->eip);
			break;
		case 1:
			modrm_ea += insn_fetch(s8, 1, c->eip);
			break;
		case 2:
			modrm_ea += insn_fetch(u16, 2, c->eip);
			break;
		}
		switch (c->modrm_rm) {
		case 0:
			modrm_ea += bx + si;
			break;
		case 1:
			modrm_ea += bx + di;
			break;
		case 2:
			modrm_ea += bp + si;
			break;
		case 3:
			modrm_ea += bp + di;
			break;
		case 4:
			modrm_ea += si;
			break;
		case 5:
			modrm_ea += di;
			break;
		case 6:
			if (c->modrm_mod != 0)
				modrm_ea += bp;
			break;
		case 7:
			modrm_ea += bx;
			break;
		}
		if (c->modrm_rm == 2 || c->modrm_rm == 3 ||
		    (c->modrm_rm == 6 && c->modrm_mod != 0))
			c->modrm_seg = VCPU_SREG_SS;
		modrm_ea = (u16)modrm_ea;
	} else {
		/* 32/64-bit ModR/M decode. */
		if ((c->modrm_rm & 7) == 4) {
			sib = insn_fetch(u8, 1, c->eip);
			index_reg |= (sib >> 3) & 7;
			base_reg |= sib & 7;
			scale = sib >> 6;

			if ((base_reg & 7) == 5 && c->modrm_mod == 0)
				modrm_ea += insn_fetch(s32, 4, c->eip);
			else
				modrm_ea += c->regs[base_reg];
			if (index_reg != 4)
				modrm_ea += c->regs[index_reg] << scale;
		} else if ((c->modrm_rm & 7) == 5 && c->modrm_mod == 0) {
			if (ctxt->mode == X86EMUL_MODE_PROT64)
				c->rip_relative = 1;
		} else
			modrm_ea += c->regs[c->modrm_rm];
		switch (c->modrm_mod) {
		case 0:
			if (c->modrm_rm == 5)
				modrm_ea += insn_fetch(s32, 4, c->eip);
			break;
		case 1:
			modrm_ea += insn_fetch(s8, 1, c->eip);
			break;
		case 2:
			modrm_ea += insn_fetch(s32, 4, c->eip);
			break;
		}
	}
	op->addr.mem.ea = modrm_ea;
done:
	return rc;
}

static int decode_abs(struct x86_emulate_ctxt *ctxt,
		      struct x86_emulate_ops *ops,
		      struct operand *op)
{
	struct decode_cache *c = &ctxt->decode;
	int rc = X86EMUL_CONTINUE;

	op->type = OP_MEM;
	switch (c->ad_bytes) {
	case 2:
		op->addr.mem.ea = insn_fetch(u16, 2, c->eip);
		break;
	case 4:
		op->addr.mem.ea = insn_fetch(u32, 4, c->eip);
		break;
	case 8:
		op->addr.mem.ea = insn_fetch(u64, 8, c->eip);
		break;
	}
done:
	return rc;
}

static void fetch_bit_operand(struct decode_cache *c)
{
	long sv = 0, mask;

	if (c->dst.type == OP_MEM && c->src.type == OP_REG) {
		mask = ~(c->dst.bytes * 8 - 1);

		if (c->src.bytes == 2)
			sv = (s16)c->src.val & (s16)mask;
		else if (c->src.bytes == 4)
			sv = (s32)c->src.val & (s32)mask;

		c->dst.addr.mem.ea += (sv >> 3);
	}

	/* only subword offset */
	c->src.val &= (c->dst.bytes << 3) - 1;
}

static int read_emulated(struct x86_emulate_ctxt *ctxt,
			 struct x86_emulate_ops *ops,
			 unsigned long addr, void *dest, unsigned size)
{
	int rc;
	struct read_cache *mc = &ctxt->decode.mem_read;

	while (size) {
		int n = min(size, 8u);
		size -= n;
		if (mc->pos < mc->end)
			goto read_cached;

		rc = ops->read_emulated(ctxt, addr, mc->data + mc->end, n,
					&ctxt->exception);
		if (rc != X86EMUL_CONTINUE)
			return rc;
		mc->end += n;

	read_cached:
		memcpy(dest, mc->data + mc->pos, n);
		mc->pos += n;
		dest += n;
		addr += n;
	}
	return X86EMUL_CONTINUE;
}

static int segmented_read(struct x86_emulate_ctxt *ctxt,
			  struct segmented_address addr,
			  void *data,
			  unsigned size)
{
	int rc;
	ulong linear;

	rc = linearize(ctxt, addr, size, false, &linear);
	if (rc != X86EMUL_CONTINUE)
		return rc;
	return read_emulated(ctxt, ctxt->ops, linear, data, size);
}

static int segmented_write(struct x86_emulate_ctxt *ctxt,
			   struct segmented_address addr,
			   const void *data,
			   unsigned size)
{
	int rc;
	ulong linear;

	rc = linearize(ctxt, addr, size, true, &linear);
	if (rc != X86EMUL_CONTINUE)
		return rc;
	return ctxt->ops->write_emulated(ctxt, linear, data, size,
					 &ctxt->exception);
}

static int segmented_cmpxchg(struct x86_emulate_ctxt *ctxt,
			     struct segmented_address addr,
			     const void *orig_data, const void *data,
			     unsigned size)
{
	int rc;
	ulong linear;

	rc = linearize(ctxt, addr, size, true, &linear);
	if (rc != X86EMUL_CONTINUE)
		return rc;
	return ctxt->ops->cmpxchg_emulated(ctxt, linear, orig_data, data,
					   size, &ctxt->exception);
}

static int pio_in_emulated(struct x86_emulate_ctxt *ctxt,
			   struct x86_emulate_ops *ops,
			   unsigned int size, unsigned short port,
			   void *dest)
{
	struct read_cache *rc = &ctxt->decode.io_read;

	if (rc->pos == rc->end) { /* refill pio read ahead */
		struct decode_cache *c = &ctxt->decode;
		unsigned int in_page, n;
		unsigned int count = c->rep_prefix ?
			address_mask(c, c->regs[VCPU_REGS_RCX]) : 1;
		in_page = (ctxt->eflags & EFLG_DF) ?
			offset_in_page(c->regs[VCPU_REGS_RDI]) :
			PAGE_SIZE - offset_in_page(c->regs[VCPU_REGS_RDI]);
		n = min(min(in_page, (unsigned int)sizeof(rc->data)) / size,
			count);
		if (n == 0)
			n = 1;
		rc->pos = rc->end = 0;
		if (!ops->pio_in_emulated(ctxt, size, port, rc->data, n))
			return 0;
		rc->end = n * size;
	}

	memcpy(dest, rc->data + rc->pos, size);
	rc->pos += size;
	return 1;
}

static void get_descriptor_table_ptr(struct x86_emulate_ctxt *ctxt,
				     struct x86_emulate_ops *ops,
				     u16 selector, struct desc_ptr *dt)
{
	if (selector & 1 << 2) {
		struct desc_struct desc;
		u16 sel;

		memset (dt, 0, sizeof *dt);
		if (!ops->get_segment(ctxt, &sel, &desc, NULL, VCPU_SREG_LDTR))
			return;

		dt->size = desc_limit_scaled(&desc); /* what if limit > 65535? */
		dt->address = get_desc_base(&desc);
	} else
		ops->get_gdt(ctxt, dt);
}

/* allowed just for 8 bytes segments */
static int read_segment_descriptor(struct x86_emulate_ctxt *ctxt,
				   struct x86_emulate_ops *ops,
				   u16 selector, struct desc_struct *desc)
{
	struct desc_ptr dt;
	u16 index = selector >> 3;
	int ret;
	ulong addr;

	get_descriptor_table_ptr(ctxt, ops, selector, &dt);

	if (dt.size < index * 8 + 7)
		return emulate_gp(ctxt, selector & 0xfffc);
	addr = dt.address + index * 8;
	ret = ops->read_std(ctxt, addr, desc, sizeof *desc, &ctxt->exception);

       return ret;
}

/* allowed just for 8 bytes segments */
static int write_segment_descriptor(struct x86_emulate_ctxt *ctxt,
				    struct x86_emulate_ops *ops,
				    u16 selector, struct desc_struct *desc)
{
	struct desc_ptr dt;
	u16 index = selector >> 3;
	ulong addr;
	int ret;

	get_descriptor_table_ptr(ctxt, ops, selector, &dt);

	if (dt.size < index * 8 + 7)
		return emulate_gp(ctxt, selector & 0xfffc);

	addr = dt.address + index * 8;
	ret = ops->write_std(ctxt, addr, desc, sizeof *desc, &ctxt->exception);

	return ret;
}

/* Does not support long mode */
static int load_segment_descriptor(struct x86_emulate_ctxt *ctxt,
				   struct x86_emulate_ops *ops,
				   u16 selector, int seg)
{
	struct desc_struct seg_desc;
	u8 dpl, rpl, cpl;
	unsigned err_vec = GP_VECTOR;
	u32 err_code = 0;
	bool null_selector = !(selector & ~0x3); /* 0000-0003 are null */
	int ret;

	memset(&seg_desc, 0, sizeof seg_desc);

	if ((seg <= VCPU_SREG_GS && ctxt->mode == X86EMUL_MODE_VM86)
	    || ctxt->mode == X86EMUL_MODE_REAL) {
		/* set real mode segment descriptor */
		set_desc_base(&seg_desc, selector << 4);
		set_desc_limit(&seg_desc, 0xffff);
		seg_desc.type = 3;
		seg_desc.p = 1;
		seg_desc.s = 1;
		goto load;
	}

	/* NULL selector is not valid for TR, CS and SS */
	if ((seg == VCPU_SREG_CS || seg == VCPU_SREG_SS || seg == VCPU_SREG_TR)
	    && null_selector)
		goto exception;

	/* TR should be in GDT only */
	if (seg == VCPU_SREG_TR && (selector & (1 << 2)))
		goto exception;

	if (null_selector) /* for NULL selector skip all following checks */
		goto load;

	ret = read_segment_descriptor(ctxt, ops, selector, &seg_desc);
	if (ret != X86EMUL_CONTINUE)
		return ret;

	err_code = selector & 0xfffc;
	err_vec = GP_VECTOR;

	/* can't load system descriptor into segment selecor */
	if (seg <= VCPU_SREG_GS && !seg_desc.s)
		goto exception;

	if (!seg_desc.p) {
		err_vec = (seg == VCPU_SREG_SS) ? SS_VECTOR : NP_VECTOR;
		goto exception;
	}

	rpl = selector & 3;
	dpl = seg_desc.dpl;
	cpl = ops->cpl(ctxt);

	switch (seg) {
	case VCPU_SREG_SS:
		/*
		 * segment is not a writable data segment or segment
		 * selector's RPL != CPL or segment selector's RPL != CPL
		 */
		if (rpl != cpl || (seg_desc.type & 0xa) != 0x2 || dpl != cpl)
			goto exception;
		break;
	case VCPU_SREG_CS:
		if (!(seg_desc.type & 8))
			goto exception;

		if (seg_desc.type & 4) {
			/* conforming */
			if (dpl > cpl)
				goto exception;
		} else {
			/* nonconforming */
			if (rpl > cpl || dpl != cpl)
				goto exception;
		}
		/* CS(RPL) <- CPL */
		selector = (selector & 0xfffc) | cpl;
		break;
	case VCPU_SREG_TR:
		if (seg_desc.s || (seg_desc.type != 1 && seg_desc.type != 9))
			goto exception;
		break;
	case VCPU_SREG_LDTR:
		if (seg_desc.s || seg_desc.type != 2)
			goto exception;
		break;
	default: /*  DS, ES, FS, or GS */
		/*
		 * segment is not a data or readable code segment or
		 * ((segment is a data or nonconforming code segment)
		 * and (both RPL and CPL > DPL))
		 */
		if ((seg_desc.type & 0xa) == 0x8 ||
		    (((seg_desc.type & 0xc) != 0xc) &&
		     (rpl > dpl && cpl > dpl)))
			goto exception;
		break;
	}

	if (seg_desc.s) {
		/* mark segment as accessed */
		seg_desc.type |= 1;
		ret = write_segment_descriptor(ctxt, ops, selector, &seg_desc);
		if (ret != X86EMUL_CONTINUE)
			return ret;
	}
load:
	ops->set_segment(ctxt, selector, &seg_desc, 0, seg);
	return X86EMUL_CONTINUE;
exception:
	emulate_exception(ctxt, err_vec, err_code, true);
	return X86EMUL_PROPAGATE_FAULT;
}

static void write_register_operand(struct operand *op)
{
	/* The 4-byte case *is* correct: in 64-bit mode we zero-extend. */
	switch (op->bytes) {
	case 1:
		*(u8 *)op->addr.reg = (u8)op->val;
		break;
	case 2:
		*(u16 *)op->addr.reg = (u16)op->val;
		break;
	case 4:
		*op->addr.reg = (u32)op->val;
		break;	/* 64b: zero-extend */
	case 8:
		*op->addr.reg = op->val;
		break;
	}
}

static int writeback(struct x86_emulate_ctxt *ctxt)
{
	int rc;
	struct decode_cache *c = &ctxt->decode;

	switch (c->dst.type) {
	case OP_REG:
		write_register_operand(&c->dst);
		break;
	case OP_MEM:
		if (c->lock_prefix)
			rc = segmented_cmpxchg(ctxt,
					       c->dst.addr.mem,
					       &c->dst.orig_val,
					       &c->dst.val,
					       c->dst.bytes);
		else
			rc = segmented_write(ctxt,
					     c->dst.addr.mem,
					     &c->dst.val,
					     c->dst.bytes);
		if (rc != X86EMUL_CONTINUE)
			return rc;
		break;
	case OP_XMM:
		write_sse_reg(ctxt, &c->dst.vec_val, c->dst.addr.xmm);
		break;
	case OP_NONE:
		/* no writeback */
		break;
	default:
		break;
	}
	return X86EMUL_CONTINUE;
}

static int em_push(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	struct segmented_address addr;

	register_address_increment(c, &c->regs[VCPU_REGS_RSP], -c->op_bytes);
	addr.ea = register_address(c, c->regs[VCPU_REGS_RSP]);
	addr.seg = VCPU_SREG_SS;

	/* Disable writeback. */
	c->dst.type = OP_NONE;
	return segmented_write(ctxt, addr, &c->src.val, c->op_bytes);
}

static int emulate_pop(struct x86_emulate_ctxt *ctxt,
		       void *dest, int len)
{
	struct decode_cache *c = &ctxt->decode;
	int rc;
	struct segmented_address addr;

	addr.ea = register_address(c, c->regs[VCPU_REGS_RSP]);
	addr.seg = VCPU_SREG_SS;
	rc = segmented_read(ctxt, addr, dest, len);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	register_address_increment(c, &c->regs[VCPU_REGS_RSP], len);
	return rc;
}

static int em_pop(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	return emulate_pop(ctxt, &c->dst.val, c->op_bytes);
}

static int emulate_popf(struct x86_emulate_ctxt *ctxt,
		       struct x86_emulate_ops *ops,
		       void *dest, int len)
{
	int rc;
	unsigned long val, change_mask;
	int iopl = (ctxt->eflags & X86_EFLAGS_IOPL) >> IOPL_SHIFT;
	int cpl = ops->cpl(ctxt);

	rc = emulate_pop(ctxt, &val, len);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	change_mask = EFLG_CF | EFLG_PF | EFLG_AF | EFLG_ZF | EFLG_SF | EFLG_OF
		| EFLG_TF | EFLG_DF | EFLG_NT | EFLG_RF | EFLG_AC | EFLG_ID;

	switch(ctxt->mode) {
	case X86EMUL_MODE_PROT64:
	case X86EMUL_MODE_PROT32:
	case X86EMUL_MODE_PROT16:
		if (cpl == 0)
			change_mask |= EFLG_IOPL;
		if (cpl <= iopl)
			change_mask |= EFLG_IF;
		break;
	case X86EMUL_MODE_VM86:
		if (iopl < 3)
			return emulate_gp(ctxt, 0);
		change_mask |= EFLG_IF;
		break;
	default: /* real mode */
		change_mask |= (EFLG_IOPL | EFLG_IF);
		break;
	}

	*(unsigned long *)dest =
		(ctxt->eflags & ~change_mask) | (val & change_mask);

	return rc;
}

static int em_popf(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	c->dst.type = OP_REG;
	c->dst.addr.reg = &ctxt->eflags;
	c->dst.bytes = c->op_bytes;
	return emulate_popf(ctxt, ctxt->ops, &c->dst.val, c->op_bytes);
}

static int emulate_push_sreg(struct x86_emulate_ctxt *ctxt,
			     struct x86_emulate_ops *ops, int seg)
{
	struct decode_cache *c = &ctxt->decode;

	c->src.val = get_segment_selector(ctxt, seg);

	return em_push(ctxt);
}

static int emulate_pop_sreg(struct x86_emulate_ctxt *ctxt,
			     struct x86_emulate_ops *ops, int seg)
{
	struct decode_cache *c = &ctxt->decode;
	unsigned long selector;
	int rc;

	rc = emulate_pop(ctxt, &selector, c->op_bytes);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	rc = load_segment_descriptor(ctxt, ops, (u16)selector, seg);
	return rc;
}

static int em_pusha(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	unsigned long old_esp = c->regs[VCPU_REGS_RSP];
	int rc = X86EMUL_CONTINUE;
	int reg = VCPU_REGS_RAX;

	while (reg <= VCPU_REGS_RDI) {
		(reg == VCPU_REGS_RSP) ?
		(c->src.val = old_esp) : (c->src.val = c->regs[reg]);

		rc = em_push(ctxt);
		if (rc != X86EMUL_CONTINUE)
			return rc;

		++reg;
	}

	return rc;
}

static int em_pushf(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	c->src.val =  (unsigned long)ctxt->eflags;
	return em_push(ctxt);
}

static int em_popa(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	int rc = X86EMUL_CONTINUE;
	int reg = VCPU_REGS_RDI;

	while (reg >= VCPU_REGS_RAX) {
		if (reg == VCPU_REGS_RSP) {
			register_address_increment(c, &c->regs[VCPU_REGS_RSP],
							c->op_bytes);
			--reg;
		}

		rc = emulate_pop(ctxt, &c->regs[reg], c->op_bytes);
		if (rc != X86EMUL_CONTINUE)
			break;
		--reg;
	}
	return rc;
}

int emulate_int_real(struct x86_emulate_ctxt *ctxt,
			       struct x86_emulate_ops *ops, int irq)
{
	struct decode_cache *c = &ctxt->decode;
	int rc;
	struct desc_ptr dt;
	gva_t cs_addr;
	gva_t eip_addr;
	u16 cs, eip;

	/* TODO: Add limit checks */
	c->src.val = ctxt->eflags;
	rc = em_push(ctxt);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	ctxt->eflags &= ~(EFLG_IF | EFLG_TF | EFLG_AC);

	c->src.val = get_segment_selector(ctxt, VCPU_SREG_CS);
	rc = em_push(ctxt);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	c->src.val = c->eip;
	rc = em_push(ctxt);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	ops->get_idt(ctxt, &dt);

	eip_addr = dt.address + (irq << 2);
	cs_addr = dt.address + (irq << 2) + 2;

	rc = ops->read_std(ctxt, cs_addr, &cs, 2, &ctxt->exception);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	rc = ops->read_std(ctxt, eip_addr, &eip, 2, &ctxt->exception);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	rc = load_segment_descriptor(ctxt, ops, cs, VCPU_SREG_CS);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	c->eip = eip;

	return rc;
}

static int emulate_int(struct x86_emulate_ctxt *ctxt,
		       struct x86_emulate_ops *ops, int irq)
{
	switch(ctxt->mode) {
	case X86EMUL_MODE_REAL:
		return emulate_int_real(ctxt, ops, irq);
	case X86EMUL_MODE_VM86:
	case X86EMUL_MODE_PROT16:
	case X86EMUL_MODE_PROT32:
	case X86EMUL_MODE_PROT64:
	default:
		/* Protected mode interrupts unimplemented yet */
		return X86EMUL_UNHANDLEABLE;
	}
}

static int emulate_iret_real(struct x86_emulate_ctxt *ctxt,
			     struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	int rc = X86EMUL_CONTINUE;
	unsigned long temp_eip = 0;
	unsigned long temp_eflags = 0;
	unsigned long cs = 0;
	unsigned long mask = EFLG_CF | EFLG_PF | EFLG_AF | EFLG_ZF | EFLG_SF | EFLG_TF |
			     EFLG_IF | EFLG_DF | EFLG_OF | EFLG_IOPL | EFLG_NT | EFLG_RF |
			     EFLG_AC | EFLG_ID | (1 << 1); /* Last one is the reserved bit */
	unsigned long vm86_mask = EFLG_VM | EFLG_VIF | EFLG_VIP;

	/* TODO: Add stack limit check */

	rc = emulate_pop(ctxt, &temp_eip, c->op_bytes);

	if (rc != X86EMUL_CONTINUE)
		return rc;

	if (temp_eip & ~0xffff)
		return emulate_gp(ctxt, 0);

	rc = emulate_pop(ctxt, &cs, c->op_bytes);

	if (rc != X86EMUL_CONTINUE)
		return rc;

	rc = emulate_pop(ctxt, &temp_eflags, c->op_bytes);

	if (rc != X86EMUL_CONTINUE)
		return rc;

	rc = load_segment_descriptor(ctxt, ops, (u16)cs, VCPU_SREG_CS);

	if (rc != X86EMUL_CONTINUE)
		return rc;

	c->eip = temp_eip;


	if (c->op_bytes == 4)
		ctxt->eflags = ((temp_eflags & mask) | (ctxt->eflags & vm86_mask));
	else if (c->op_bytes == 2) {
		ctxt->eflags &= ~0xffff;
		ctxt->eflags |= temp_eflags;
	}

	ctxt->eflags &= ~EFLG_RESERVED_ZEROS_MASK; /* Clear reserved zeros */
	ctxt->eflags |= EFLG_RESERVED_ONE_MASK;

	return rc;
}

static inline int emulate_iret(struct x86_emulate_ctxt *ctxt,
				    struct x86_emulate_ops* ops)
{
	switch(ctxt->mode) {
	case X86EMUL_MODE_REAL:
		return emulate_iret_real(ctxt, ops);
	case X86EMUL_MODE_VM86:
	case X86EMUL_MODE_PROT16:
	case X86EMUL_MODE_PROT32:
	case X86EMUL_MODE_PROT64:
	default:
		/* iret from protected mode unimplemented yet */
		return X86EMUL_UNHANDLEABLE;
	}
}

static int em_jmp_far(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	int rc;
	unsigned short sel;

	memcpy(&sel, c->src.valptr + c->op_bytes, 2);

	rc = load_segment_descriptor(ctxt, ctxt->ops, sel, VCPU_SREG_CS);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	c->eip = 0;
	memcpy(&c->eip, c->src.valptr, c->op_bytes);
	return X86EMUL_CONTINUE;
}

static int em_grp1a(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	return emulate_pop(ctxt, &c->dst.val, c->dst.bytes);
}

static int em_grp2(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	switch (c->modrm_reg) {
	case 0:	/* rol */
		emulate_2op_SrcB("rol", c->src, c->dst, ctxt->eflags);
		break;
	case 1:	/* ror */
		emulate_2op_SrcB("ror", c->src, c->dst, ctxt->eflags);
		break;
	case 2:	/* rcl */
		emulate_2op_SrcB("rcl", c->src, c->dst, ctxt->eflags);
		break;
	case 3:	/* rcr */
		emulate_2op_SrcB("rcr", c->src, c->dst, ctxt->eflags);
		break;
	case 4:	/* sal/shl */
	case 6:	/* sal/shl */
		emulate_2op_SrcB("sal", c->src, c->dst, ctxt->eflags);
		break;
	case 5:	/* shr */
		emulate_2op_SrcB("shr", c->src, c->dst, ctxt->eflags);
		break;
	case 7:	/* sar */
		emulate_2op_SrcB("sar", c->src, c->dst, ctxt->eflags);
		break;
	}
	return X86EMUL_CONTINUE;
}

static int em_grp3(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	unsigned long *rax = &c->regs[VCPU_REGS_RAX];
	unsigned long *rdx = &c->regs[VCPU_REGS_RDX];
	u8 de = 0;

	switch (c->modrm_reg) {
	case 0 ... 1:	/* test */
		emulate_2op_SrcV("test", c->src, c->dst, ctxt->eflags);
		break;
	case 2:	/* not */
		c->dst.val = ~c->dst.val;
		break;
	case 3:	/* neg */
		emulate_1op("neg", c->dst, ctxt->eflags);
		break;
	case 4: /* mul */
		emulate_1op_rax_rdx("mul", c->src, *rax, *rdx, ctxt->eflags);
		break;
	case 5: /* imul */
		emulate_1op_rax_rdx("imul", c->src, *rax, *rdx, ctxt->eflags);
		break;
	case 6: /* div */
		emulate_1op_rax_rdx_ex("div", c->src, *rax, *rdx,
				       ctxt->eflags, de);
		break;
	case 7: /* idiv */
		emulate_1op_rax_rdx_ex("idiv", c->src, *rax, *rdx,
				       ctxt->eflags, de);
		break;
	default:
		return X86EMUL_UNHANDLEABLE;
	}
	if (de)
		return emulate_de(ctxt);
	return X86EMUL_CONTINUE;
}

static int em_grp45(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	int rc = X86EMUL_CONTINUE;

	switch (c->modrm_reg) {
	case 0:	/* inc */
		emulate_1op("inc", c->dst, ctxt->eflags);
		break;
	case 1:	/* dec */
		emulate_1op("dec", c->dst, ctxt->eflags);
		break;
	case 2: /* call near abs */ {
		long int old_eip;
		old_eip = c->eip;
		c->eip = c->src.val;
		c->src.val = old_eip;
		rc = em_push(ctxt);
		break;
	}
	case 4: /* jmp abs */
		c->eip = c->src.val;
		break;
	case 5: /* jmp far */
		rc = em_jmp_far(ctxt);
		break;
	case 6:	/* push */
		rc = em_push(ctxt);
		break;
	}
	return rc;
}

static int em_grp9(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	u64 old = c->dst.orig_val64;

	if (((u32) (old >> 0) != (u32) c->regs[VCPU_REGS_RAX]) ||
	    ((u32) (old >> 32) != (u32) c->regs[VCPU_REGS_RDX])) {
		c->regs[VCPU_REGS_RAX] = (u32) (old >> 0);
		c->regs[VCPU_REGS_RDX] = (u32) (old >> 32);
		ctxt->eflags &= ~EFLG_ZF;
	} else {
		c->dst.val64 = ((u64)c->regs[VCPU_REGS_RCX] << 32) |
			(u32) c->regs[VCPU_REGS_RBX];

		ctxt->eflags |= EFLG_ZF;
	}
	return X86EMUL_CONTINUE;
}

static int emulate_ret_far(struct x86_emulate_ctxt *ctxt,
			   struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	int rc;
	unsigned long cs;

	rc = emulate_pop(ctxt, &c->eip, c->op_bytes);
	if (rc != X86EMUL_CONTINUE)
		return rc;
	if (c->op_bytes == 4)
		c->eip = (u32)c->eip;
	rc = emulate_pop(ctxt, &cs, c->op_bytes);
	if (rc != X86EMUL_CONTINUE)
		return rc;
	rc = load_segment_descriptor(ctxt, ops, (u16)cs, VCPU_SREG_CS);
	return rc;
}

static int emulate_load_segment(struct x86_emulate_ctxt *ctxt,
			   struct x86_emulate_ops *ops, int seg)
{
	struct decode_cache *c = &ctxt->decode;
	unsigned short sel;
	int rc;

	memcpy(&sel, c->src.valptr + c->op_bytes, 2);

	rc = load_segment_descriptor(ctxt, ops, sel, seg);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	c->dst.val = c->src.val;
	return rc;
}

static inline void
setup_syscalls_segments(struct x86_emulate_ctxt *ctxt,
			struct x86_emulate_ops *ops, struct desc_struct *cs,
			struct desc_struct *ss)
{
	u16 selector;

	memset(cs, 0, sizeof(struct desc_struct));
	ops->get_segment(ctxt, &selector, cs, NULL, VCPU_SREG_CS);
	memset(ss, 0, sizeof(struct desc_struct));

	cs->l = 0;		/* will be adjusted later */
	set_desc_base(cs, 0);	/* flat segment */
	cs->g = 1;		/* 4kb granularity */
	set_desc_limit(cs, 0xfffff);	/* 4GB limit */
	cs->type = 0x0b;	/* Read, Execute, Accessed */
	cs->s = 1;
	cs->dpl = 0;		/* will be adjusted later */
	cs->p = 1;
	cs->d = 1;

	set_desc_base(ss, 0);	/* flat segment */
	set_desc_limit(ss, 0xfffff);	/* 4GB limit */
	ss->g = 1;		/* 4kb granularity */
	ss->s = 1;
	ss->type = 0x03;	/* Read/Write, Accessed */
	ss->d = 1;		/* 32bit stack segment */
	ss->dpl = 0;
	ss->p = 1;
}

static int
emulate_syscall(struct x86_emulate_ctxt *ctxt, struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	struct desc_struct cs, ss;
	u64 msr_data;
	u16 cs_sel, ss_sel;
	u64 efer = 0;

	/* syscall is not available in real mode */
	if (ctxt->mode == X86EMUL_MODE_REAL ||
	    ctxt->mode == X86EMUL_MODE_VM86)
		return emulate_ud(ctxt);

	ops->get_msr(ctxt, MSR_EFER, &efer);
	setup_syscalls_segments(ctxt, ops, &cs, &ss);

	ops->get_msr(ctxt, MSR_STAR, &msr_data);
	msr_data >>= 32;
	cs_sel = (u16)(msr_data & 0xfffc);
	ss_sel = (u16)(msr_data + 8);

	if (efer & EFER_LMA) {
		cs.d = 0;
		cs.l = 1;
	}
	ops->set_segment(ctxt, cs_sel, &cs, 0, VCPU_SREG_CS);
	ops->set_segment(ctxt, ss_sel, &ss, 0, VCPU_SREG_SS);

	c->regs[VCPU_REGS_RCX] = c->eip;
	if (efer & EFER_LMA) {
#ifdef CONFIG_X86_64
		c->regs[VCPU_REGS_R11] = ctxt->eflags & ~EFLG_RF;

		ops->get_msr(ctxt,
			     ctxt->mode == X86EMUL_MODE_PROT64 ?
			     MSR_LSTAR : MSR_CSTAR, &msr_data);
		c->eip = msr_data;

		ops->get_msr(ctxt, MSR_SYSCALL_MASK, &msr_data);
		ctxt->eflags &= ~(msr_data | EFLG_RF);
#endif
	} else {
		/* legacy mode */
		ops->get_msr(ctxt, MSR_STAR, &msr_data);
		c->eip = (u32)msr_data;

		ctxt->eflags &= ~(EFLG_VM | EFLG_IF | EFLG_RF);
	}

	return X86EMUL_CONTINUE;
}

static int
emulate_sysenter(struct x86_emulate_ctxt *ctxt, struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	struct desc_struct cs, ss;
	u64 msr_data;
	u16 cs_sel, ss_sel;
	u64 efer = 0;

	ctxt->ops->get_msr(ctxt, MSR_EFER, &efer);
	/* inject #GP if in real mode */
	if (ctxt->mode == X86EMUL_MODE_REAL)
		return emulate_gp(ctxt, 0);

	/* XXX sysenter/sysexit have not been tested in 64bit mode.
	* Therefore, we inject an #UD.
	*/
	if (ctxt->mode == X86EMUL_MODE_PROT64)
		return emulate_ud(ctxt);

	setup_syscalls_segments(ctxt, ops, &cs, &ss);

	ops->get_msr(ctxt, MSR_IA32_SYSENTER_CS, &msr_data);
	switch (ctxt->mode) {
	case X86EMUL_MODE_PROT32:
		if ((msr_data & 0xfffc) == 0x0)
			return emulate_gp(ctxt, 0);
		break;
	case X86EMUL_MODE_PROT64:
		if (msr_data == 0x0)
			return emulate_gp(ctxt, 0);
		break;
	}

	ctxt->eflags &= ~(EFLG_VM | EFLG_IF | EFLG_RF);
	cs_sel = (u16)msr_data;
	cs_sel &= ~SELECTOR_RPL_MASK;
	ss_sel = cs_sel + 8;
	ss_sel &= ~SELECTOR_RPL_MASK;
	if (ctxt->mode == X86EMUL_MODE_PROT64 || (efer & EFER_LMA)) {
		cs.d = 0;
		cs.l = 1;
	}

	ops->set_segment(ctxt, cs_sel, &cs, 0, VCPU_SREG_CS);
	ops->set_segment(ctxt, ss_sel, &ss, 0, VCPU_SREG_SS);

	ops->get_msr(ctxt, MSR_IA32_SYSENTER_EIP, &msr_data);
	c->eip = msr_data;

	ops->get_msr(ctxt, MSR_IA32_SYSENTER_ESP, &msr_data);
	c->regs[VCPU_REGS_RSP] = msr_data;

	return X86EMUL_CONTINUE;
}

static int
emulate_sysexit(struct x86_emulate_ctxt *ctxt, struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	struct desc_struct cs, ss;
	u64 msr_data;
	int usermode;
	u16 cs_sel, ss_sel;

	/* inject #GP if in real mode or Virtual 8086 mode */
	if (ctxt->mode == X86EMUL_MODE_REAL ||
	    ctxt->mode == X86EMUL_MODE_VM86)
		return emulate_gp(ctxt, 0);

	setup_syscalls_segments(ctxt, ops, &cs, &ss);

	if ((c->rex_prefix & 0x8) != 0x0)
		usermode = X86EMUL_MODE_PROT64;
	else
		usermode = X86EMUL_MODE_PROT32;

	cs.dpl = 3;
	ss.dpl = 3;
	ops->get_msr(ctxt, MSR_IA32_SYSENTER_CS, &msr_data);
	switch (usermode) {
	case X86EMUL_MODE_PROT32:
		cs_sel = (u16)(msr_data + 16);
		if ((msr_data & 0xfffc) == 0x0)
			return emulate_gp(ctxt, 0);
		ss_sel = (u16)(msr_data + 24);
		break;
	case X86EMUL_MODE_PROT64:
		cs_sel = (u16)(msr_data + 32);
		if (msr_data == 0x0)
			return emulate_gp(ctxt, 0);
		ss_sel = cs_sel + 8;
		cs.d = 0;
		cs.l = 1;
		break;
	}
	cs_sel |= SELECTOR_RPL_MASK;
	ss_sel |= SELECTOR_RPL_MASK;

	ops->set_segment(ctxt, cs_sel, &cs, 0, VCPU_SREG_CS);
	ops->set_segment(ctxt, ss_sel, &ss, 0, VCPU_SREG_SS);

	c->eip = c->regs[VCPU_REGS_RDX];
	c->regs[VCPU_REGS_RSP] = c->regs[VCPU_REGS_RCX];

	return X86EMUL_CONTINUE;
}

static bool emulator_bad_iopl(struct x86_emulate_ctxt *ctxt,
			      struct x86_emulate_ops *ops)
{
	int iopl;
	if (ctxt->mode == X86EMUL_MODE_REAL)
		return false;
	if (ctxt->mode == X86EMUL_MODE_VM86)
		return true;
	iopl = (ctxt->eflags & X86_EFLAGS_IOPL) >> IOPL_SHIFT;
	return ops->cpl(ctxt) > iopl;
}

static bool emulator_io_port_access_allowed(struct x86_emulate_ctxt *ctxt,
					    struct x86_emulate_ops *ops,
					    u16 port, u16 len)
{
	struct desc_struct tr_seg;
	u32 base3;
	int r;
	u16 tr, io_bitmap_ptr, perm, bit_idx = port & 0x7;
	unsigned mask = (1 << len) - 1;
	unsigned long base;

	ops->get_segment(ctxt, &tr, &tr_seg, &base3, VCPU_SREG_TR);
	if (!tr_seg.p)
		return false;
	if (desc_limit_scaled(&tr_seg) < 103)
		return false;
	base = get_desc_base(&tr_seg);
#ifdef CONFIG_X86_64
	base |= ((u64)base3) << 32;
#endif
	r = ops->read_std(ctxt, base + 102, &io_bitmap_ptr, 2, NULL);
	if (r != X86EMUL_CONTINUE)
		return false;
	if (io_bitmap_ptr + port/8 > desc_limit_scaled(&tr_seg))
		return false;
	r = ops->read_std(ctxt, base + io_bitmap_ptr + port/8, &perm, 2, NULL);
	if (r != X86EMUL_CONTINUE)
		return false;
	if ((perm >> bit_idx) & mask)
		return false;
	return true;
}

static bool emulator_io_permited(struct x86_emulate_ctxt *ctxt,
				 struct x86_emulate_ops *ops,
				 u16 port, u16 len)
{
	if (ctxt->perm_ok)
		return true;

	if (emulator_bad_iopl(ctxt, ops))
		if (!emulator_io_port_access_allowed(ctxt, ops, port, len))
			return false;

	ctxt->perm_ok = true;

	return true;
}

static void save_state_to_tss16(struct x86_emulate_ctxt *ctxt,
				struct x86_emulate_ops *ops,
				struct tss_segment_16 *tss)
{
	struct decode_cache *c = &ctxt->decode;

	tss->ip = c->eip;
	tss->flag = ctxt->eflags;
	tss->ax = c->regs[VCPU_REGS_RAX];
	tss->cx = c->regs[VCPU_REGS_RCX];
	tss->dx = c->regs[VCPU_REGS_RDX];
	tss->bx = c->regs[VCPU_REGS_RBX];
	tss->sp = c->regs[VCPU_REGS_RSP];
	tss->bp = c->regs[VCPU_REGS_RBP];
	tss->si = c->regs[VCPU_REGS_RSI];
	tss->di = c->regs[VCPU_REGS_RDI];

	tss->es = get_segment_selector(ctxt, VCPU_SREG_ES);
	tss->cs = get_segment_selector(ctxt, VCPU_SREG_CS);
	tss->ss = get_segment_selector(ctxt, VCPU_SREG_SS);
	tss->ds = get_segment_selector(ctxt, VCPU_SREG_DS);
	tss->ldt = get_segment_selector(ctxt, VCPU_SREG_LDTR);
}

static int load_state_from_tss16(struct x86_emulate_ctxt *ctxt,
				 struct x86_emulate_ops *ops,
				 struct tss_segment_16 *tss)
{
	struct decode_cache *c = &ctxt->decode;
	int ret;

	c->eip = tss->ip;
	ctxt->eflags = tss->flag | 2;
	c->regs[VCPU_REGS_RAX] = tss->ax;
	c->regs[VCPU_REGS_RCX] = tss->cx;
	c->regs[VCPU_REGS_RDX] = tss->dx;
	c->regs[VCPU_REGS_RBX] = tss->bx;
	c->regs[VCPU_REGS_RSP] = tss->sp;
	c->regs[VCPU_REGS_RBP] = tss->bp;
	c->regs[VCPU_REGS_RSI] = tss->si;
	c->regs[VCPU_REGS_RDI] = tss->di;

	/*
	 * SDM says that segment selectors are loaded before segment
	 * descriptors
	 */
	set_segment_selector(ctxt, tss->ldt, VCPU_SREG_LDTR);
	set_segment_selector(ctxt, tss->es, VCPU_SREG_ES);
	set_segment_selector(ctxt, tss->cs, VCPU_SREG_CS);
	set_segment_selector(ctxt, tss->ss, VCPU_SREG_SS);
	set_segment_selector(ctxt, tss->ds, VCPU_SREG_DS);

	/*
	 * Now load segment descriptors. If fault happenes at this stage
	 * it is handled in a context of new task
	 */
	ret = load_segment_descriptor(ctxt, ops, tss->ldt, VCPU_SREG_LDTR);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->es, VCPU_SREG_ES);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->cs, VCPU_SREG_CS);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->ss, VCPU_SREG_SS);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->ds, VCPU_SREG_DS);
	if (ret != X86EMUL_CONTINUE)
		return ret;

	return X86EMUL_CONTINUE;
}

static int task_switch_16(struct x86_emulate_ctxt *ctxt,
			  struct x86_emulate_ops *ops,
			  u16 tss_selector, u16 old_tss_sel,
			  ulong old_tss_base, struct desc_struct *new_desc)
{
	struct tss_segment_16 tss_seg;
	int ret;
	u32 new_tss_base = get_desc_base(new_desc);

	ret = ops->read_std(ctxt, old_tss_base, &tss_seg, sizeof tss_seg,
			    &ctxt->exception);
	if (ret != X86EMUL_CONTINUE)
		/* FIXME: need to provide precise fault address */
		return ret;

	save_state_to_tss16(ctxt, ops, &tss_seg);

	ret = ops->write_std(ctxt, old_tss_base, &tss_seg, sizeof tss_seg,
			     &ctxt->exception);
	if (ret != X86EMUL_CONTINUE)
		/* FIXME: need to provide precise fault address */
		return ret;

	ret = ops->read_std(ctxt, new_tss_base, &tss_seg, sizeof tss_seg,
			    &ctxt->exception);
	if (ret != X86EMUL_CONTINUE)
		/* FIXME: need to provide precise fault address */
		return ret;

	if (old_tss_sel != 0xffff) {
		tss_seg.prev_task_link = old_tss_sel;

		ret = ops->write_std(ctxt, new_tss_base,
				     &tss_seg.prev_task_link,
				     sizeof tss_seg.prev_task_link,
				     &ctxt->exception);
		if (ret != X86EMUL_CONTINUE)
			/* FIXME: need to provide precise fault address */
			return ret;
	}

	return load_state_from_tss16(ctxt, ops, &tss_seg);
}

static void save_state_to_tss32(struct x86_emulate_ctxt *ctxt,
				struct x86_emulate_ops *ops,
				struct tss_segment_32 *tss)
{
	struct decode_cache *c = &ctxt->decode;

	tss->cr3 = ops->get_cr(ctxt, 3);
	tss->eip = c->eip;
	tss->eflags = ctxt->eflags;
	tss->eax = c->regs[VCPU_REGS_RAX];
	tss->ecx = c->regs[VCPU_REGS_RCX];
	tss->edx = c->regs[VCPU_REGS_RDX];
	tss->ebx = c->regs[VCPU_REGS_RBX];
	tss->esp = c->regs[VCPU_REGS_RSP];
	tss->ebp = c->regs[VCPU_REGS_RBP];
	tss->esi = c->regs[VCPU_REGS_RSI];
	tss->edi = c->regs[VCPU_REGS_RDI];

	tss->es = get_segment_selector(ctxt, VCPU_SREG_ES);
	tss->cs = get_segment_selector(ctxt, VCPU_SREG_CS);
	tss->ss = get_segment_selector(ctxt, VCPU_SREG_SS);
	tss->ds = get_segment_selector(ctxt, VCPU_SREG_DS);
	tss->fs = get_segment_selector(ctxt, VCPU_SREG_FS);
	tss->gs = get_segment_selector(ctxt, VCPU_SREG_GS);
	tss->ldt_selector = get_segment_selector(ctxt, VCPU_SREG_LDTR);
}

static int load_state_from_tss32(struct x86_emulate_ctxt *ctxt,
				 struct x86_emulate_ops *ops,
				 struct tss_segment_32 *tss)
{
	struct decode_cache *c = &ctxt->decode;
	int ret;

	if (ops->set_cr(ctxt, 3, tss->cr3))
		return emulate_gp(ctxt, 0);
	c->eip = tss->eip;
	ctxt->eflags = tss->eflags | 2;
	c->regs[VCPU_REGS_RAX] = tss->eax;
	c->regs[VCPU_REGS_RCX] = tss->ecx;
	c->regs[VCPU_REGS_RDX] = tss->edx;
	c->regs[VCPU_REGS_RBX] = tss->ebx;
	c->regs[VCPU_REGS_RSP] = tss->esp;
	c->regs[VCPU_REGS_RBP] = tss->ebp;
	c->regs[VCPU_REGS_RSI] = tss->esi;
	c->regs[VCPU_REGS_RDI] = tss->edi;

	/*
	 * SDM says that segment selectors are loaded before segment
	 * descriptors
	 */
	set_segment_selector(ctxt, tss->ldt_selector, VCPU_SREG_LDTR);
	set_segment_selector(ctxt, tss->es, VCPU_SREG_ES);
	set_segment_selector(ctxt, tss->cs, VCPU_SREG_CS);
	set_segment_selector(ctxt, tss->ss, VCPU_SREG_SS);
	set_segment_selector(ctxt, tss->ds, VCPU_SREG_DS);
	set_segment_selector(ctxt, tss->fs, VCPU_SREG_FS);
	set_segment_selector(ctxt, tss->gs, VCPU_SREG_GS);

	/*
	 * Now load segment descriptors. If fault happenes at this stage
	 * it is handled in a context of new task
	 */
	ret = load_segment_descriptor(ctxt, ops, tss->ldt_selector, VCPU_SREG_LDTR);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->es, VCPU_SREG_ES);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->cs, VCPU_SREG_CS);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->ss, VCPU_SREG_SS);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->ds, VCPU_SREG_DS);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->fs, VCPU_SREG_FS);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->gs, VCPU_SREG_GS);
	if (ret != X86EMUL_CONTINUE)
		return ret;

	return X86EMUL_CONTINUE;
}

static int task_switch_32(struct x86_emulate_ctxt *ctxt,
			  struct x86_emulate_ops *ops,
			  u16 tss_selector, u16 old_tss_sel,
			  ulong old_tss_base, struct desc_struct *new_desc)
{
	struct tss_segment_32 tss_seg;
	int ret;
	u32 new_tss_base = get_desc_base(new_desc);

	ret = ops->read_std(ctxt, old_tss_base, &tss_seg, sizeof tss_seg,
			    &ctxt->exception);
	if (ret != X86EMUL_CONTINUE)
		/* FIXME: need to provide precise fault address */
		return ret;

	save_state_to_tss32(ctxt, ops, &tss_seg);

	ret = ops->write_std(ctxt, old_tss_base, &tss_seg, sizeof tss_seg,
			     &ctxt->exception);
	if (ret != X86EMUL_CONTINUE)
		/* FIXME: need to provide precise fault address */
		return ret;

	ret = ops->read_std(ctxt, new_tss_base, &tss_seg, sizeof tss_seg,
			    &ctxt->exception);
	if (ret != X86EMUL_CONTINUE)
		/* FIXME: need to provide precise fault address */
		return ret;

	if (old_tss_sel != 0xffff) {
		tss_seg.prev_task_link = old_tss_sel;

		ret = ops->write_std(ctxt, new_tss_base,
				     &tss_seg.prev_task_link,
				     sizeof tss_seg.prev_task_link,
				     &ctxt->exception);
		if (ret != X86EMUL_CONTINUE)
			/* FIXME: need to provide precise fault address */
			return ret;
	}

	return load_state_from_tss32(ctxt, ops, &tss_seg);
}

static int emulator_do_task_switch(struct x86_emulate_ctxt *ctxt,
				   struct x86_emulate_ops *ops,
				   u16 tss_selector, int reason,
				   bool has_error_code, u32 error_code)
{
	struct desc_struct curr_tss_desc, next_tss_desc;
	int ret;
	u16 old_tss_sel = get_segment_selector(ctxt, VCPU_SREG_TR);
	ulong old_tss_base =
		ops->get_cached_segment_base(ctxt, VCPU_SREG_TR);
	u32 desc_limit;

	/* FIXME: old_tss_base == ~0 ? */

	ret = read_segment_descriptor(ctxt, ops, tss_selector, &next_tss_desc);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = read_segment_descriptor(ctxt, ops, old_tss_sel, &curr_tss_desc);
	if (ret != X86EMUL_CONTINUE)
		return ret;

	/* FIXME: check that next_tss_desc is tss */

	if (reason != TASK_SWITCH_IRET) {
		if ((tss_selector & 3) > next_tss_desc.dpl ||
		    ops->cpl(ctxt) > next_tss_desc.dpl)
			return emulate_gp(ctxt, 0);
	}

	desc_limit = desc_limit_scaled(&next_tss_desc);
	if (!next_tss_desc.p ||
	    ((desc_limit < 0x67 && (next_tss_desc.type & 8)) ||
	     desc_limit < 0x2b)) {
		emulate_ts(ctxt, tss_selector & 0xfffc);
		return X86EMUL_PROPAGATE_FAULT;
	}

	if (reason == TASK_SWITCH_IRET || reason == TASK_SWITCH_JMP) {
		curr_tss_desc.type &= ~(1 << 1); /* clear busy flag */
		write_segment_descriptor(ctxt, ops, old_tss_sel,
					 &curr_tss_desc);
	}

	if (reason == TASK_SWITCH_IRET)
		ctxt->eflags = ctxt->eflags & ~X86_EFLAGS_NT;

	/* set back link to prev task only if NT bit is set in eflags
	   note that old_tss_sel is not used afetr this point */
	if (reason != TASK_SWITCH_CALL && reason != TASK_SWITCH_GATE)
		old_tss_sel = 0xffff;

	if (next_tss_desc.type & 8)
		ret = task_switch_32(ctxt, ops, tss_selector, old_tss_sel,
				     old_tss_base, &next_tss_desc);
	else
		ret = task_switch_16(ctxt, ops, tss_selector, old_tss_sel,
				     old_tss_base, &next_tss_desc);
	if (ret != X86EMUL_CONTINUE)
		return ret;

	if (reason == TASK_SWITCH_CALL || reason == TASK_SWITCH_GATE)
		ctxt->eflags = ctxt->eflags | X86_EFLAGS_NT;

	if (reason != TASK_SWITCH_IRET) {
		next_tss_desc.type |= (1 << 1); /* set busy flag */
		write_segment_descriptor(ctxt, ops, tss_selector,
					 &next_tss_desc);
	}

	ops->set_cr(ctxt, 0,  ops->get_cr(ctxt, 0) | X86_CR0_TS);
	ops->set_segment(ctxt, tss_selector, &next_tss_desc, 0, VCPU_SREG_TR);

	if (has_error_code) {
		struct decode_cache *c = &ctxt->decode;

		c->op_bytes = c->ad_bytes = (next_tss_desc.type & 8) ? 4 : 2;
		c->lock_prefix = 0;
		c->src.val = (unsigned long) error_code;
		ret = em_push(ctxt);
	}

	return ret;
}

int emulator_task_switch(struct x86_emulate_ctxt *ctxt,
			 u16 tss_selector, int reason,
			 bool has_error_code, u32 error_code)
{
	struct x86_emulate_ops *ops = ctxt->ops;
	struct decode_cache *c = &ctxt->decode;
	int rc;

	c->eip = ctxt->eip;
	c->dst.type = OP_NONE;

	rc = emulator_do_task_switch(ctxt, ops, tss_selector, reason,
				     has_error_code, error_code);

	if (rc == X86EMUL_CONTINUE)
		ctxt->eip = c->eip;

	return (rc == X86EMUL_UNHANDLEABLE) ? EMULATION_FAILED : EMULATION_OK;
}

static void string_addr_inc(struct x86_emulate_ctxt *ctxt, unsigned seg,
			    int reg, struct operand *op)
{
	struct decode_cache *c = &ctxt->decode;
	int df = (ctxt->eflags & EFLG_DF) ? -1 : 1;

	register_address_increment(c, &c->regs[reg], df * op->bytes);
	op->addr.mem.ea = register_address(c, c->regs[reg]);
	op->addr.mem.seg = seg;
}

static int em_das(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	u8 al, old_al;
	bool af, cf, old_cf;

	cf = ctxt->eflags & X86_EFLAGS_CF;
	al = c->dst.val;

	old_al = al;
	old_cf = cf;
	cf = false;
	af = ctxt->eflags & X86_EFLAGS_AF;
	if ((al & 0x0f) > 9 || af) {
		al -= 6;
		cf = old_cf | (al >= 250);
		af = true;
	} else {
		af = false;
	}
	if (old_al > 0x99 || old_cf) {
		al -= 0x60;
		cf = true;
	}

	c->dst.val = al;
	/* Set PF, ZF, SF */
	c->src.type = OP_IMM;
	c->src.val = 0;
	c->src.bytes = 1;
	emulate_2op_SrcV("or", c->src, c->dst, ctxt->eflags);
	ctxt->eflags &= ~(X86_EFLAGS_AF | X86_EFLAGS_CF);
	if (cf)
		ctxt->eflags |= X86_EFLAGS_CF;
	if (af)
		ctxt->eflags |= X86_EFLAGS_AF;
	return X86EMUL_CONTINUE;
}

static int em_call_far(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	u16 sel, old_cs;
	ulong old_eip;
	int rc;

	old_cs = get_segment_selector(ctxt, VCPU_SREG_CS);
	old_eip = c->eip;

	memcpy(&sel, c->src.valptr + c->op_bytes, 2);
	if (load_segment_descriptor(ctxt, ctxt->ops, sel, VCPU_SREG_CS))
		return X86EMUL_CONTINUE;

	c->eip = 0;
	memcpy(&c->eip, c->src.valptr, c->op_bytes);

	c->src.val = old_cs;
	rc = em_push(ctxt);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	c->src.val = old_eip;
	return em_push(ctxt);
}

static int em_ret_near_imm(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	int rc;

	c->dst.type = OP_REG;
	c->dst.addr.reg = &c->eip;
	c->dst.bytes = c->op_bytes;
	rc = emulate_pop(ctxt, &c->dst.val, c->op_bytes);
	if (rc != X86EMUL_CONTINUE)
		return rc;
	register_address_increment(c, &c->regs[VCPU_REGS_RSP], c->src.val);
	return X86EMUL_CONTINUE;
}

static int em_add(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	emulate_2op_SrcV("add", c->src, c->dst, ctxt->eflags);
	return X86EMUL_CONTINUE;
}

static int em_or(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	emulate_2op_SrcV("or", c->src, c->dst, ctxt->eflags);
	return X86EMUL_CONTINUE;
}

static int em_adc(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	emulate_2op_SrcV("adc", c->src, c->dst, ctxt->eflags);
	return X86EMUL_CONTINUE;
}

static int em_sbb(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	emulate_2op_SrcV("sbb", c->src, c->dst, ctxt->eflags);
	return X86EMUL_CONTINUE;
}

static int em_and(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	emulate_2op_SrcV("and", c->src, c->dst, ctxt->eflags);
	return X86EMUL_CONTINUE;
}

static int em_sub(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	emulate_2op_SrcV("sub", c->src, c->dst, ctxt->eflags);
	return X86EMUL_CONTINUE;
}

static int em_xor(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	emulate_2op_SrcV("xor", c->src, c->dst, ctxt->eflags);
	return X86EMUL_CONTINUE;
}

static int em_cmp(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	emulate_2op_SrcV("cmp", c->src, c->dst, ctxt->eflags);
	/* Disable writeback. */
	c->dst.type = OP_NONE;
	return X86EMUL_CONTINUE;
}

static int em_imul(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	emulate_2op_SrcV_nobyte("imul", c->src, c->dst, ctxt->eflags);
	return X86EMUL_CONTINUE;
}

static int em_imul_3op(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	c->dst.val = c->src2.val;
	return em_imul(ctxt);
}

static int em_cwd(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	c->dst.type = OP_REG;
	c->dst.bytes = c->src.bytes;
	c->dst.addr.reg = &c->regs[VCPU_REGS_RDX];
	c->dst.val = ~((c->src.val >> (c->src.bytes * 8 - 1)) - 1);

	return X86EMUL_CONTINUE;
}

static int em_rdtsc(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	u64 tsc = 0;

	ctxt->ops->get_msr(ctxt, MSR_IA32_TSC, &tsc);
	c->regs[VCPU_REGS_RAX] = (u32)tsc;
	c->regs[VCPU_REGS_RDX] = tsc >> 32;
	return X86EMUL_CONTINUE;
}

static int em_mov(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	c->dst.val = c->src.val;
	return X86EMUL_CONTINUE;
}

static int em_movdqu(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	memcpy(&c->dst.vec_val, &c->src.vec_val, c->op_bytes);
	return X86EMUL_CONTINUE;
}

static int em_invlpg(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	int rc;
	ulong linear;

	rc = linearize(ctxt, c->src.addr.mem, 1, false, &linear);
	if (rc == X86EMUL_CONTINUE)
		ctxt->ops->invlpg(ctxt, linear);
	/* Disable writeback. */
	c->dst.type = OP_NONE;
	return X86EMUL_CONTINUE;
}

static int em_clts(struct x86_emulate_ctxt *ctxt)
{
	ulong cr0;

	cr0 = ctxt->ops->get_cr(ctxt, 0);
	cr0 &= ~X86_CR0_TS;
	ctxt->ops->set_cr(ctxt, 0, cr0);
	return X86EMUL_CONTINUE;
}

static int em_vmcall(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	int rc;

	if (c->modrm_mod != 3 || c->modrm_rm != 1)
		return X86EMUL_UNHANDLEABLE;

	rc = ctxt->ops->fix_hypercall(ctxt);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	/* Let the processor re-execute the fixed hypercall */
	c->eip = ctxt->eip;
	/* Disable writeback. */
	c->dst.type = OP_NONE;
	return X86EMUL_CONTINUE;
}

static int em_lgdt(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	struct desc_ptr desc_ptr;
	int rc;

	rc = read_descriptor(ctxt, c->src.addr.mem,
			     &desc_ptr.size, &desc_ptr.address,
			     c->op_bytes);
	if (rc != X86EMUL_CONTINUE)
		return rc;
	ctxt->ops->set_gdt(ctxt, &desc_ptr);
	/* Disable writeback. */
	c->dst.type = OP_NONE;
	return X86EMUL_CONTINUE;
}

static int em_vmmcall(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	int rc;

	rc = ctxt->ops->fix_hypercall(ctxt);

	/* Disable writeback. */
	c->dst.type = OP_NONE;
	return rc;
}

static int em_lidt(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	struct desc_ptr desc_ptr;
	int rc;

	rc = read_descriptor(ctxt, c->src.addr.mem,
			     &desc_ptr.size, &desc_ptr.address,
			     c->op_bytes);
	if (rc != X86EMUL_CONTINUE)
		return rc;
	ctxt->ops->set_idt(ctxt, &desc_ptr);
	/* Disable writeback. */
	c->dst.type = OP_NONE;
	return X86EMUL_CONTINUE;
}

static int em_smsw(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	c->dst.bytes = 2;
	c->dst.val = ctxt->ops->get_cr(ctxt, 0);
	return X86EMUL_CONTINUE;
}

static int em_lmsw(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	ctxt->ops->set_cr(ctxt, 0, (ctxt->ops->get_cr(ctxt, 0) & ~0x0eul)
			  | (c->src.val & 0x0f));
	c->dst.type = OP_NONE;
	return X86EMUL_CONTINUE;
}

static bool valid_cr(int nr)
{
	switch (nr) {
	case 0:
	case 2 ... 4:
	case 8:
		return true;
	default:
		return false;
	}
}

static int check_cr_read(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	if (!valid_cr(c->modrm_reg))
		return emulate_ud(ctxt);

	return X86EMUL_CONTINUE;
}

static int check_cr_write(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	u64 new_val = c->src.val64;
	int cr = c->modrm_reg;
	u64 efer = 0;

	static u64 cr_reserved_bits[] = {
		0xffffffff00000000ULL,
		0, 0, 0, /* CR3 checked later */
		CR4_RESERVED_BITS,
		0, 0, 0,
		CR8_RESERVED_BITS,
	};

	if (!valid_cr(cr))
		return emulate_ud(ctxt);

	if (new_val & cr_reserved_bits[cr])
		return emulate_gp(ctxt, 0);

	switch (cr) {
	case 0: {
		u64 cr4;
		if (((new_val & X86_CR0_PG) && !(new_val & X86_CR0_PE)) ||
		    ((new_val & X86_CR0_NW) && !(new_val & X86_CR0_CD)))
			return emulate_gp(ctxt, 0);

		cr4 = ctxt->ops->get_cr(ctxt, 4);
		ctxt->ops->get_msr(ctxt, MSR_EFER, &efer);

		if ((new_val & X86_CR0_PG) && (efer & EFER_LME) &&
		    !(cr4 & X86_CR4_PAE))
			return emulate_gp(ctxt, 0);

		break;
		}
	case 3: {
		u64 rsvd = 0;

		ctxt->ops->get_msr(ctxt, MSR_EFER, &efer);
		if (efer & EFER_LMA)
			rsvd = CR3_L_MODE_RESERVED_BITS;
		else if (ctxt->ops->get_cr(ctxt, 4) & X86_CR4_PAE)
			rsvd = CR3_PAE_RESERVED_BITS;
		else if (ctxt->ops->get_cr(ctxt, 0) & X86_CR0_PG)
			rsvd = CR3_NONPAE_RESERVED_BITS;

		if (new_val & rsvd)
			return emulate_gp(ctxt, 0);

		break;
		}
	case 4: {
		u64 cr4;

		cr4 = ctxt->ops->get_cr(ctxt, 4);
		ctxt->ops->get_msr(ctxt, MSR_EFER, &efer);

		if ((efer & EFER_LMA) && !(new_val & X86_CR4_PAE))
			return emulate_gp(ctxt, 0);

		break;
		}
	}

	return X86EMUL_CONTINUE;
}

static int check_dr7_gd(struct x86_emulate_ctxt *ctxt)
{
	unsigned long dr7;

	ctxt->ops->get_dr(ctxt, 7, &dr7);

	/* Check if DR7.Global_Enable is set */
	return dr7 & (1 << 13);
}

static int check_dr_read(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	int dr = c->modrm_reg;
	u64 cr4;

	if (dr > 7)
		return emulate_ud(ctxt);

	cr4 = ctxt->ops->get_cr(ctxt, 4);
	if ((cr4 & X86_CR4_DE) && (dr == 4 || dr == 5))
		return emulate_ud(ctxt);

	if (check_dr7_gd(ctxt))
		return emulate_db(ctxt);

	return X86EMUL_CONTINUE;
}

static int check_dr_write(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	u64 new_val = c->src.val64;
	int dr = c->modrm_reg;

	if ((dr == 6 || dr == 7) && (new_val & 0xffffffff00000000ULL))
		return emulate_gp(ctxt, 0);

	return check_dr_read(ctxt);
}

static int check_svme(struct x86_emulate_ctxt *ctxt)
{
	u64 efer;

	ctxt->ops->get_msr(ctxt, MSR_EFER, &efer);

	if (!(efer & EFER_SVME))
		return emulate_ud(ctxt);

	return X86EMUL_CONTINUE;
}

static int check_svme_pa(struct x86_emulate_ctxt *ctxt)
{
	u64 rax = ctxt->decode.regs[VCPU_REGS_RAX];

	/* Valid physical address? */
	if (rax & 0xffff000000000000ULL)
		return emulate_gp(ctxt, 0);

	return check_svme(ctxt);
}

static int check_rdtsc(struct x86_emulate_ctxt *ctxt)
{
	u64 cr4 = ctxt->ops->get_cr(ctxt, 4);

	if (cr4 & X86_CR4_TSD && ctxt->ops->cpl(ctxt))
		return emulate_ud(ctxt);

	return X86EMUL_CONTINUE;
}

static int check_rdpmc(struct x86_emulate_ctxt *ctxt)
{
	u64 cr4 = ctxt->ops->get_cr(ctxt, 4);
	u64 rcx = ctxt->decode.regs[VCPU_REGS_RCX];

	if ((!(cr4 & X86_CR4_PCE) && ctxt->ops->cpl(ctxt)) ||
	    (rcx > 3))
		return emulate_gp(ctxt, 0);

	return X86EMUL_CONTINUE;
}

static int check_perm_in(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	c->dst.bytes = min(c->dst.bytes, 4u);
	if (!emulator_io_permited(ctxt, ctxt->ops, c->src.val, c->dst.bytes))
		return emulate_gp(ctxt, 0);

	return X86EMUL_CONTINUE;
}

static int check_perm_out(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	c->src.bytes = min(c->src.bytes, 4u);
	if (!emulator_io_permited(ctxt, ctxt->ops, c->dst.val, c->src.bytes))
		return emulate_gp(ctxt, 0);

	return X86EMUL_CONTINUE;
}

#define D(_y) { .flags = (_y) }
#define DI(_y, _i) { .flags = (_y), .intercept = x86_intercept_##_i }
#define DIP(_y, _i, _p) { .flags = (_y), .intercept = x86_intercept_##_i, \
		      .check_perm = (_p) }
#define N    D(0)
#define EXT(_f, _e) { .flags = ((_f) | RMExt), .u.group = (_e) }
#define G(_f, _g) { .flags = ((_f) | Group), .u.group = (_g) }
#define GD(_f, _g) { .flags = ((_f) | GroupDual), .u.gdual = (_g) }
#define I(_f, _e) { .flags = (_f), .u.execute = (_e) }
#define II(_f, _e, _i) \
	{ .flags = (_f), .u.execute = (_e), .intercept = x86_intercept_##_i }
#define IIP(_f, _e, _i, _p) \
	{ .flags = (_f), .u.execute = (_e), .intercept = x86_intercept_##_i, \
	  .check_perm = (_p) }
#define GP(_f, _g) { .flags = ((_f) | Prefix), .u.gprefix = (_g) }

#define D2bv(_f)      D((_f) | ByteOp), D(_f)
#define D2bvIP(_f, _i, _p) DIP((_f) | ByteOp, _i, _p), DIP(_f, _i, _p)
#define I2bv(_f, _e)  I((_f) | ByteOp, _e), I(_f, _e)

#define I6ALU(_f, _e) I2bv((_f) | DstMem | SrcReg | ModRM, _e),		\
		I2bv(((_f) | DstReg | SrcMem | ModRM) & ~Lock, _e),	\
		I2bv(((_f) & ~Lock) | DstAcc | SrcImm, _e)

static struct opcode group7_rm1[] = {
	DI(SrcNone | ModRM | Priv, monitor),
	DI(SrcNone | ModRM | Priv, mwait),
	N, N, N, N, N, N,
};

static struct opcode group7_rm3[] = {
	DIP(SrcNone | ModRM | Prot | Priv, vmrun,   check_svme_pa),
	II(SrcNone | ModRM | Prot | VendorSpecific, em_vmmcall, vmmcall),
	DIP(SrcNone | ModRM | Prot | Priv, vmload,  check_svme_pa),
	DIP(SrcNone | ModRM | Prot | Priv, vmsave,  check_svme_pa),
	DIP(SrcNone | ModRM | Prot | Priv, stgi,    check_svme),
	DIP(SrcNone | ModRM | Prot | Priv, clgi,    check_svme),
	DIP(SrcNone | ModRM | Prot | Priv, skinit,  check_svme),
	DIP(SrcNone | ModRM | Prot | Priv, invlpga, check_svme),
};

static struct opcode group7_rm7[] = {
	N,
	DIP(SrcNone | ModRM, rdtscp, check_rdtsc),
	N, N, N, N, N, N,
};

static struct opcode group1[] = {
	I(Lock, em_add),
	I(Lock, em_or),
	I(Lock, em_adc),
	I(Lock, em_sbb),
	I(Lock, em_and),
	I(Lock, em_sub),
	I(Lock, em_xor),
	I(0, em_cmp),
};

static struct opcode group1A[] = {
	D(DstMem | SrcNone | ModRM | Mov | Stack), N, N, N, N, N, N, N,
};

static struct opcode group3[] = {
	D(DstMem | SrcImm | ModRM), D(DstMem | SrcImm | ModRM),
	D(DstMem | SrcNone | ModRM | Lock), D(DstMem | SrcNone | ModRM | Lock),
	X4(D(SrcMem | ModRM)),
};

static struct opcode group4[] = {
	D(ByteOp | DstMem | SrcNone | ModRM | Lock), D(ByteOp | DstMem | SrcNone | ModRM | Lock),
	N, N, N, N, N, N,
};

static struct opcode group5[] = {
	D(DstMem | SrcNone | ModRM | Lock), D(DstMem | SrcNone | ModRM | Lock),
	D(SrcMem | ModRM | Stack),
	I(SrcMemFAddr | ModRM | ImplicitOps | Stack, em_call_far),
	D(SrcMem | ModRM | Stack), D(SrcMemFAddr | ModRM | ImplicitOps),
	D(SrcMem | ModRM | Stack), N,
};

static struct opcode group6[] = {
	DI(ModRM | Prot,        sldt),
	DI(ModRM | Prot,        str),
	DI(ModRM | Prot | Priv, lldt),
	DI(ModRM | Prot | Priv, ltr),
	N, N, N, N,
};

static struct group_dual group7 = { {
	DI(ModRM | Mov | DstMem | Priv, sgdt),
	DI(ModRM | Mov | DstMem | Priv, sidt),
	II(ModRM | SrcMem | Priv, em_lgdt, lgdt),
	II(ModRM | SrcMem | Priv, em_lidt, lidt),
	II(SrcNone | ModRM | DstMem | Mov, em_smsw, smsw), N,
	II(SrcMem16 | ModRM | Mov | Priv, em_lmsw, lmsw),
	II(SrcMem | ModRM | ByteOp | Priv | NoAccess, em_invlpg, invlpg),
}, {
	I(SrcNone | ModRM | Priv | VendorSpecific, em_vmcall),
	EXT(0, group7_rm1),
	N, EXT(0, group7_rm3),
	II(SrcNone | ModRM | DstMem | Mov, em_smsw, smsw), N,
	II(SrcMem16 | ModRM | Mov | Priv, em_lmsw, lmsw), EXT(0, group7_rm7),
} };

static struct opcode group8[] = {
	N, N, N, N,
	D(DstMem | SrcImmByte | ModRM), D(DstMem | SrcImmByte | ModRM | Lock),
	D(DstMem | SrcImmByte | ModRM | Lock), D(DstMem | SrcImmByte | ModRM | Lock),
};

static struct group_dual group9 = { {
	N, D(DstMem64 | ModRM | Lock), N, N, N, N, N, N,
}, {
	N, N, N, N, N, N, N, N,
} };

static struct opcode group11[] = {
	I(DstMem | SrcImm | ModRM | Mov, em_mov), X7(D(Undefined)),
};

static struct gprefix pfx_0f_6f_0f_7f = {
	N, N, N, I(Sse, em_movdqu),
};

static struct opcode opcode_table[256] = {
	/* 0x00 - 0x07 */
	I6ALU(Lock, em_add),
	D(ImplicitOps | Stack | No64), D(ImplicitOps | Stack | No64),
	/* 0x08 - 0x0F */
	I6ALU(Lock, em_or),
	D(ImplicitOps | Stack | No64), N,
	/* 0x10 - 0x17 */
	I6ALU(Lock, em_adc),
	D(ImplicitOps | Stack | No64), D(ImplicitOps | Stack | No64),
	/* 0x18 - 0x1F */
	I6ALU(Lock, em_sbb),
	D(ImplicitOps | Stack | No64), D(ImplicitOps | Stack | No64),
	/* 0x20 - 0x27 */
	I6ALU(Lock, em_and), N, N,
	/* 0x28 - 0x2F */
	I6ALU(Lock, em_sub), N, I(ByteOp | DstAcc | No64, em_das),
	/* 0x30 - 0x37 */
	I6ALU(Lock, em_xor), N, N,
	/* 0x38 - 0x3F */
	I6ALU(0, em_cmp), N, N,
	/* 0x40 - 0x4F */
	X16(D(DstReg)),
	/* 0x50 - 0x57 */
	X8(I(SrcReg | Stack, em_push)),
	/* 0x58 - 0x5F */
	X8(I(DstReg | Stack, em_pop)),
	/* 0x60 - 0x67 */
	I(ImplicitOps | Stack | No64, em_pusha),
	I(ImplicitOps | Stack | No64, em_popa),
	N, D(DstReg | SrcMem32 | ModRM | Mov) /* movsxd (x86/64) */ ,
	N, N, N, N,
	/* 0x68 - 0x6F */
	I(SrcImm | Mov | Stack, em_push),
	I(DstReg | SrcMem | ModRM | Src2Imm, em_imul_3op),
	I(SrcImmByte | Mov | Stack, em_push),
	I(DstReg | SrcMem | ModRM | Src2ImmByte, em_imul_3op),
	D2bvIP(DstDI | SrcDX | Mov | String, ins, check_perm_in), /* insb, insw/insd */
	D2bvIP(SrcSI | DstDX | String, outs, check_perm_out), /* outsb, outsw/outsd */
	/* 0x70 - 0x7F */
	X16(D(SrcImmByte)),
	/* 0x80 - 0x87 */
	G(ByteOp | DstMem | SrcImm | ModRM | Group, group1),
	G(DstMem | SrcImm | ModRM | Group, group1),
	G(ByteOp | DstMem | SrcImm | ModRM | No64 | Group, group1),
	G(DstMem | SrcImmByte | ModRM | Group, group1),
	D2bv(DstMem | SrcReg | ModRM), D2bv(DstMem | SrcReg | ModRM | Lock),
	/* 0x88 - 0x8F */
	I2bv(DstMem | SrcReg | ModRM | Mov, em_mov),
	I2bv(DstReg | SrcMem | ModRM | Mov, em_mov),
	D(DstMem | SrcNone | ModRM | Mov), D(ModRM | SrcMem | NoAccess | DstReg),
	D(ImplicitOps | SrcMem16 | ModRM), G(0, group1A),
	/* 0x90 - 0x97 */
	DI(SrcAcc | DstReg, pause), X7(D(SrcAcc | DstReg)),
	/* 0x98 - 0x9F */
	D(DstAcc | SrcNone), I(ImplicitOps | SrcAcc, em_cwd),
	I(SrcImmFAddr | No64, em_call_far), N,
	II(ImplicitOps | Stack, em_pushf, pushf),
	II(ImplicitOps | Stack, em_popf, popf), N, N,
	/* 0xA0 - 0xA7 */
	I2bv(DstAcc | SrcMem | Mov | MemAbs, em_mov),
	I2bv(DstMem | SrcAcc | Mov | MemAbs, em_mov),
	I2bv(SrcSI | DstDI | Mov | String, em_mov),
	I2bv(SrcSI | DstDI | String, em_cmp),
	/* 0xA8 - 0xAF */
	D2bv(DstAcc | SrcImm),
	I2bv(SrcAcc | DstDI | Mov | String, em_mov),
	I2bv(SrcSI | DstAcc | Mov | String, em_mov),
	I2bv(SrcAcc | DstDI | String, em_cmp),
	/* 0xB0 - 0xB7 */
	X8(I(ByteOp | DstReg | SrcImm | Mov, em_mov)),
	/* 0xB8 - 0xBF */
	X8(I(DstReg | SrcImm | Mov, em_mov)),
	/* 0xC0 - 0xC7 */
	D2bv(DstMem | SrcImmByte | ModRM),
	I(ImplicitOps | Stack | SrcImmU16, em_ret_near_imm),
	D(ImplicitOps | Stack),
	D(DstReg | SrcMemFAddr | ModRM | No64), D(DstReg | SrcMemFAddr | ModRM | No64),
	G(ByteOp, group11), G(0, group11),
	/* 0xC8 - 0xCF */
	N, N, N, D(ImplicitOps | Stack),
	D(ImplicitOps), DI(SrcImmByte, intn),
	D(ImplicitOps | No64), DI(ImplicitOps, iret),
	/* 0xD0 - 0xD7 */
	D2bv(DstMem | SrcOne | ModRM), D2bv(DstMem | ModRM),
	N, N, N, N,
	/* 0xD8 - 0xDF */
	N, N, N, N, N, N, N, N,
	/* 0xE0 - 0xE7 */
	X4(D(SrcImmByte)),
	D2bvIP(SrcImmUByte | DstAcc, in,  check_perm_in),
	D2bvIP(SrcAcc | DstImmUByte, out, check_perm_out),
	/* 0xE8 - 0xEF */
	D(SrcImm | Stack), D(SrcImm | ImplicitOps),
	D(SrcImmFAddr | No64), D(SrcImmByte | ImplicitOps),
	D2bvIP(SrcDX | DstAcc, in,  check_perm_in),
	D2bvIP(SrcAcc | DstDX, out, check_perm_out),
	/* 0xF0 - 0xF7 */
	N, DI(ImplicitOps, icebp), N, N,
	DI(ImplicitOps | Priv, hlt), D(ImplicitOps),
	G(ByteOp, group3), G(0, group3),
	/* 0xF8 - 0xFF */
	D(ImplicitOps), D(ImplicitOps), D(ImplicitOps), D(ImplicitOps),
	D(ImplicitOps), D(ImplicitOps), G(0, group4), G(0, group5),
};

static struct opcode twobyte_table[256] = {
	/* 0x00 - 0x0F */
	G(0, group6), GD(0, &group7), N, N,
	N, D(ImplicitOps | VendorSpecific), DI(ImplicitOps | Priv, clts), N,
	DI(ImplicitOps | Priv, invd), DI(ImplicitOps | Priv, wbinvd), N, N,
	N, D(ImplicitOps | ModRM), N, N,
	/* 0x10 - 0x1F */
	N, N, N, N, N, N, N, N, D(ImplicitOps | ModRM), N, N, N, N, N, N, N,
	/* 0x20 - 0x2F */
	DIP(ModRM | DstMem | Priv | Op3264, cr_read, check_cr_read),
	DIP(ModRM | DstMem | Priv | Op3264, dr_read, check_dr_read),
	DIP(ModRM | SrcMem | Priv | Op3264, cr_write, check_cr_write),
	DIP(ModRM | SrcMem | Priv | Op3264, dr_write, check_dr_write),
	N, N, N, N,
	N, N, N, N, N, N, N, N,
	/* 0x30 - 0x3F */
	DI(ImplicitOps | Priv, wrmsr),
	IIP(ImplicitOps, em_rdtsc, rdtsc, check_rdtsc),
	DI(ImplicitOps | Priv, rdmsr),
	DIP(ImplicitOps | Priv, rdpmc, check_rdpmc),
	D(ImplicitOps | VendorSpecific), D(ImplicitOps | Priv | VendorSpecific),
	N, N,
	N, N, N, N, N, N, N, N,
	/* 0x40 - 0x4F */
	X16(D(DstReg | SrcMem | ModRM | Mov)),
	/* 0x50 - 0x5F */
	N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N,
	/* 0x60 - 0x6F */
	N, N, N, N,
	N, N, N, N,
	N, N, N, N,
	N, N, N, GP(SrcMem | DstReg | ModRM | Mov, &pfx_0f_6f_0f_7f),
	/* 0x70 - 0x7F */
	N, N, N, N,
	N, N, N, N,
	N, N, N, N,
	N, N, N, GP(SrcReg | DstMem | ModRM | Mov, &pfx_0f_6f_0f_7f),
	/* 0x80 - 0x8F */
	X16(D(SrcImm)),
	/* 0x90 - 0x9F */
	X16(D(ByteOp | DstMem | SrcNone | ModRM| Mov)),
	/* 0xA0 - 0xA7 */
	D(ImplicitOps | Stack), D(ImplicitOps | Stack),
	DI(ImplicitOps, cpuid), D(DstMem | SrcReg | ModRM | BitOp),
	D(DstMem | SrcReg | Src2ImmByte | ModRM),
	D(DstMem | SrcReg | Src2CL | ModRM), N, N,
	/* 0xA8 - 0xAF */
	D(ImplicitOps | Stack), D(ImplicitOps | Stack),
	DI(ImplicitOps, rsm), D(DstMem | SrcReg | ModRM | BitOp | Lock),
	D(DstMem | SrcReg | Src2ImmByte | ModRM),
	D(DstMem | SrcReg | Src2CL | ModRM),
	D(ModRM), I(DstReg | SrcMem | ModRM, em_imul),
	/* 0xB0 - 0xB7 */
	D2bv(DstMem | SrcReg | ModRM | Lock),
	D(DstReg | SrcMemFAddr | ModRM), D(DstMem | SrcReg | ModRM | BitOp | Lock),
	D(DstReg | SrcMemFAddr | ModRM), D(DstReg | SrcMemFAddr | ModRM),
	D(ByteOp | DstReg | SrcMem | ModRM | Mov), D(DstReg | SrcMem16 | ModRM | Mov),
	/* 0xB8 - 0xBF */
	N, N,
	G(BitOp, group8), D(DstMem | SrcReg | ModRM | BitOp | Lock),
	D(DstReg | SrcMem | ModRM), D(DstReg | SrcMem | ModRM),
	D(ByteOp | DstReg | SrcMem | ModRM | Mov), D(DstReg | SrcMem16 | ModRM | Mov),
	/* 0xC0 - 0xCF */
	D2bv(DstMem | SrcReg | ModRM | Lock),
	N, D(DstMem | SrcReg | ModRM | Mov),
	N, N, N, GD(0, &group9),
	N, N, N, N, N, N, N, N,
	/* 0xD0 - 0xDF */
	N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N,
	/* 0xE0 - 0xEF */
	N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N,
	/* 0xF0 - 0xFF */
	N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N
};

#undef D
#undef N
#undef G
#undef GD
#undef I
#undef GP
#undef EXT

#undef D2bv
#undef D2bvIP
#undef I2bv
#undef I6ALU

static unsigned imm_size(struct decode_cache *c)
{
	unsigned size;

	size = (c->d & ByteOp) ? 1 : c->op_bytes;
	if (size == 8)
		size = 4;
	return size;
}

static int decode_imm(struct x86_emulate_ctxt *ctxt, struct operand *op,
		      unsigned size, bool sign_extension)
{
	struct decode_cache *c = &ctxt->decode;
	struct x86_emulate_ops *ops = ctxt->ops;
	int rc = X86EMUL_CONTINUE;

	op->type = OP_IMM;
	op->bytes = size;
	op->addr.mem.ea = c->eip;
	/* NB. Immediates are sign-extended as necessary. */
	switch (op->bytes) {
	case 1:
		op->val = insn_fetch(s8, 1, c->eip);
		break;
	case 2:
		op->val = insn_fetch(s16, 2, c->eip);
		break;
	case 4:
		op->val = insn_fetch(s32, 4, c->eip);
		break;
	}
	if (!sign_extension) {
		switch (op->bytes) {
		case 1:
			op->val &= 0xff;
			break;
		case 2:
			op->val &= 0xffff;
			break;
		case 4:
			op->val &= 0xffffffff;
			break;
		}
	}
done:
	return rc;
}

int
x86_decode_insn(struct x86_emulate_ctxt *ctxt, void *insn, int insn_len)
{
	struct x86_emulate_ops *ops = ctxt->ops;
	struct decode_cache *c = &ctxt->decode;
	int rc = X86EMUL_CONTINUE;
	int mode = ctxt->mode;
	int def_op_bytes, def_ad_bytes, goffset, simd_prefix;
	bool op_prefix = false;
	struct opcode opcode;
	struct operand memop = { .type = OP_NONE }, *memopp = NULL;

	c->eip = ctxt->eip;
	c->fetch.start = c->eip;
	c->fetch.end = c->fetch.start + insn_len;
	if (insn_len > 0)
		memcpy(c->fetch.data, insn, insn_len);

	switch (mode) {
	case X86EMUL_MODE_REAL:
	case X86EMUL_MODE_VM86:
	case X86EMUL_MODE_PROT16:
		def_op_bytes = def_ad_bytes = 2;
		break;
	case X86EMUL_MODE_PROT32:
		def_op_bytes = def_ad_bytes = 4;
		break;
#ifdef CONFIG_X86_64
	case X86EMUL_MODE_PROT64:
		def_op_bytes = 4;
		def_ad_bytes = 8;
		break;
#endif
	default:
		return -1;
	}

	c->op_bytes = def_op_bytes;
	c->ad_bytes = def_ad_bytes;

	/* Legacy prefixes. */
	for (;;) {
		switch (c->b = insn_fetch(u8, 1, c->eip)) {
		case 0x66:	/* operand-size override */
			op_prefix = true;
			/* switch between 2/4 bytes */
			c->op_bytes = def_op_bytes ^ 6;
			break;
		case 0x67:	/* address-size override */
			if (mode == X86EMUL_MODE_PROT64)
				/* switch between 4/8 bytes */
				c->ad_bytes = def_ad_bytes ^ 12;
			else
				/* switch between 2/4 bytes */
				c->ad_bytes = def_ad_bytes ^ 6;
			break;
		case 0x26:	/* ES override */
		case 0x2e:	/* CS override */
		case 0x36:	/* SS override */
		case 0x3e:	/* DS override */
			set_seg_override(c, (c->b >> 3) & 3);
			break;
		case 0x64:	/* FS override */
		case 0x65:	/* GS override */
			set_seg_override(c, c->b & 7);
			break;
		case 0x40 ... 0x4f: /* REX */
			if (mode != X86EMUL_MODE_PROT64)
				goto done_prefixes;
			c->rex_prefix = c->b;
			continue;
		case 0xf0:	/* LOCK */
			c->lock_prefix = 1;
			break;
		case 0xf2:	/* REPNE/REPNZ */
		case 0xf3:	/* REP/REPE/REPZ */
			c->rep_prefix = c->b;
			break;
		default:
			goto done_prefixes;
		}

		/* Any legacy prefix after a REX prefix nullifies its effect. */

		c->rex_prefix = 0;
	}

done_prefixes:

	/* REX prefix. */
	if (c->rex_prefix & 8)
		c->op_bytes = 8;	/* REX.W */

	/* Opcode byte(s). */
	opcode = opcode_table[c->b];
	/* Two-byte opcode? */
	if (c->b == 0x0f) {
		c->twobyte = 1;
		c->b = insn_fetch(u8, 1, c->eip);
		opcode = twobyte_table[c->b];
	}
	c->d = opcode.flags;

	while (c->d & GroupMask) {
		switch (c->d & GroupMask) {
		case Group:
			c->modrm = insn_fetch(u8, 1, c->eip);
			--c->eip;
			goffset = (c->modrm >> 3) & 7;
			opcode = opcode.u.group[goffset];
			break;
		case GroupDual:
			c->modrm = insn_fetch(u8, 1, c->eip);
			--c->eip;
			goffset = (c->modrm >> 3) & 7;
			if ((c->modrm >> 6) == 3)
				opcode = opcode.u.gdual->mod3[goffset];
			else
				opcode = opcode.u.gdual->mod012[goffset];
			break;
		case RMExt:
			goffset = c->modrm & 7;
			opcode = opcode.u.group[goffset];
			break;
		case Prefix:
			if (c->rep_prefix && op_prefix)
				return X86EMUL_UNHANDLEABLE;
			simd_prefix = op_prefix ? 0x66 : c->rep_prefix;
			switch (simd_prefix) {
			case 0x00: opcode = opcode.u.gprefix->pfx_no; break;
			case 0x66: opcode = opcode.u.gprefix->pfx_66; break;
			case 0xf2: opcode = opcode.u.gprefix->pfx_f2; break;
			case 0xf3: opcode = opcode.u.gprefix->pfx_f3; break;
			}
			break;
		default:
			return X86EMUL_UNHANDLEABLE;
		}

		c->d &= ~GroupMask;
		c->d |= opcode.flags;
	}

	c->execute = opcode.u.execute;
	c->check_perm = opcode.check_perm;
	c->intercept = opcode.intercept;

	/* Unrecognised? */
	if (c->d == 0 || (c->d & Undefined))
		return -1;

	if (!(c->d & VendorSpecific) && ctxt->only_vendor_specific_insn)
		return -1;

	if (mode == X86EMUL_MODE_PROT64 && (c->d & Stack))
		c->op_bytes = 8;

	if (c->d & Op3264) {
		if (mode == X86EMUL_MODE_PROT64)
			c->op_bytes = 8;
		else
			c->op_bytes = 4;
	}

	if (c->d & Sse)
		c->op_bytes = 16;

	/* ModRM and SIB bytes. */
	if (c->d & ModRM) {
		rc = decode_modrm(ctxt, ops, &memop);
		if (!c->has_seg_override)
			set_seg_override(c, c->modrm_seg);
	} else if (c->d & MemAbs)
		rc = decode_abs(ctxt, ops, &memop);
	if (rc != X86EMUL_CONTINUE)
		goto done;

	if (!c->has_seg_override)
		set_seg_override(c, VCPU_SREG_DS);

	memop.addr.mem.seg = seg_override(ctxt, c);

	if (memop.type == OP_MEM && c->ad_bytes != 8)
		memop.addr.mem.ea = (u32)memop.addr.mem.ea;

	/*
	 * Decode and fetch the source operand: register, memory
	 * or immediate.
	 */
	switch (c->d & SrcMask) {
	case SrcNone:
		break;
	case SrcReg:
		decode_register_operand(ctxt, &c->src, c, 0);
		break;
	case SrcMem16:
		memop.bytes = 2;
		goto srcmem_common;
	case SrcMem32:
		memop.bytes = 4;
		goto srcmem_common;
	case SrcMem:
		memop.bytes = (c->d & ByteOp) ? 1 :
							   c->op_bytes;
	srcmem_common:
		c->src = memop;
		memopp = &c->src;
		break;
	case SrcImmU16:
		rc = decode_imm(ctxt, &c->src, 2, false);
		break;
	case SrcImm:
		rc = decode_imm(ctxt, &c->src, imm_size(c), true);
		break;
	case SrcImmU:
		rc = decode_imm(ctxt, &c->src, imm_size(c), false);
		break;
	case SrcImmByte:
		rc = decode_imm(ctxt, &c->src, 1, true);
		break;
	case SrcImmUByte:
		rc = decode_imm(ctxt, &c->src, 1, false);
		break;
	case SrcAcc:
		c->src.type = OP_REG;
		c->src.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->src.addr.reg = &c->regs[VCPU_REGS_RAX];
		fetch_register_operand(&c->src);
		break;
	case SrcOne:
		c->src.bytes = 1;
		c->src.val = 1;
		break;
	case SrcSI:
		c->src.type = OP_MEM;
		c->src.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->src.addr.mem.ea =
			register_address(c, c->regs[VCPU_REGS_RSI]);
		c->src.addr.mem.seg = seg_override(ctxt, c);
		c->src.val = 0;
		break;
	case SrcImmFAddr:
		c->src.type = OP_IMM;
		c->src.addr.mem.ea = c->eip;
		c->src.bytes = c->op_bytes + 2;
		insn_fetch_arr(c->src.valptr, c->src.bytes, c->eip);
		break;
	case SrcMemFAddr:
		memop.bytes = c->op_bytes + 2;
		goto srcmem_common;
		break;
	case SrcDX:
		c->src.type = OP_REG;
		c->src.bytes = 2;
		c->src.addr.reg = &c->regs[VCPU_REGS_RDX];
		fetch_register_operand(&c->src);
		break;
	}

	if (rc != X86EMUL_CONTINUE)
		goto done;

	/*
	 * Decode and fetch the second source operand: register, memory
	 * or immediate.
	 */
	switch (c->d & Src2Mask) {
	case Src2None:
		break;
	case Src2CL:
		c->src2.bytes = 1;
		c->src2.val = c->regs[VCPU_REGS_RCX] & 0x8;
		break;
	case Src2ImmByte:
		rc = decode_imm(ctxt, &c->src2, 1, true);
		break;
	case Src2One:
		c->src2.bytes = 1;
		c->src2.val = 1;
		break;
	case Src2Imm:
		rc = decode_imm(ctxt, &c->src2, imm_size(c), true);
		break;
	}

	if (rc != X86EMUL_CONTINUE)
		goto done;

	/* Decode and fetch the destination operand: register or memory. */
	switch (c->d & DstMask) {
	case DstReg:
		decode_register_operand(ctxt, &c->dst, c,
			 c->twobyte && (c->b == 0xb6 || c->b == 0xb7));
		break;
	case DstImmUByte:
		c->dst.type = OP_IMM;
		c->dst.addr.mem.ea = c->eip;
		c->dst.bytes = 1;
		c->dst.val = insn_fetch(u8, 1, c->eip);
		break;
	case DstMem:
	case DstMem64:
		c->dst = memop;
		memopp = &c->dst;
		if ((c->d & DstMask) == DstMem64)
			c->dst.bytes = 8;
		else
			c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		if (c->d & BitOp)
			fetch_bit_operand(c);
		c->dst.orig_val = c->dst.val;
		break;
	case DstAcc:
		c->dst.type = OP_REG;
		c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->dst.addr.reg = &c->regs[VCPU_REGS_RAX];
		fetch_register_operand(&c->dst);
		c->dst.orig_val = c->dst.val;
		break;
	case DstDI:
		c->dst.type = OP_MEM;
		c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->dst.addr.mem.ea =
			register_address(c, c->regs[VCPU_REGS_RDI]);
		c->dst.addr.mem.seg = VCPU_SREG_ES;
		c->dst.val = 0;
		break;
	case DstDX:
		c->dst.type = OP_REG;
		c->dst.bytes = 2;
		c->dst.addr.reg = &c->regs[VCPU_REGS_RDX];
		fetch_register_operand(&c->dst);
		break;
	case ImplicitOps:
		/* Special instructions do their own operand decoding. */
	default:
		c->dst.type = OP_NONE; /* Disable writeback. */
		break;
	}

done:
	if (memopp && memopp->type == OP_MEM && c->rip_relative)
		memopp->addr.mem.ea += c->eip;

	return (rc == X86EMUL_UNHANDLEABLE) ? EMULATION_FAILED : EMULATION_OK;
}

static bool string_insn_completed(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	/* The second termination condition only applies for REPE
	 * and REPNE. Test if the repeat string operation prefix is
	 * REPE/REPZ or REPNE/REPNZ and if it's the case it tests the
	 * corresponding termination condition according to:
	 * 	- if REPE/REPZ and ZF = 0 then done
	 * 	- if REPNE/REPNZ and ZF = 1 then done
	 */
	if (((c->b == 0xa6) || (c->b == 0xa7) ||
	     (c->b == 0xae) || (c->b == 0xaf))
	    && (((c->rep_prefix == REPE_PREFIX) &&
		 ((ctxt->eflags & EFLG_ZF) == 0))
		|| ((c->rep_prefix == REPNE_PREFIX) &&
		    ((ctxt->eflags & EFLG_ZF) == EFLG_ZF))))
		return true;

	return false;
}

int
x86_emulate_insn(struct x86_emulate_ctxt *ctxt)
{
	struct x86_emulate_ops *ops = ctxt->ops;
	u64 msr_data;
	struct decode_cache *c = &ctxt->decode;
	int rc = X86EMUL_CONTINUE;
	int saved_dst_type = c->dst.type;
	int irq; /* Used for int 3, int, and into */

	ctxt->decode.mem_read.pos = 0;

	if (ctxt->mode == X86EMUL_MODE_PROT64 && (c->d & No64)) {
		rc = emulate_ud(ctxt);
		goto done;
	}

	/* LOCK prefix is allowed only with some instructions */
	if (c->lock_prefix && (!(c->d & Lock) || c->dst.type != OP_MEM)) {
		rc = emulate_ud(ctxt);
		goto done;
	}

	if ((c->d & SrcMask) == SrcMemFAddr && c->src.type != OP_MEM) {
		rc = emulate_ud(ctxt);
		goto done;
	}

	if ((c->d & Sse)
	    && ((ops->get_cr(ctxt, 0) & X86_CR0_EM)
		|| !(ops->get_cr(ctxt, 4) & X86_CR4_OSFXSR))) {
		rc = emulate_ud(ctxt);
		goto done;
	}

	if ((c->d & Sse) && (ops->get_cr(ctxt, 0) & X86_CR0_TS)) {
		rc = emulate_nm(ctxt);
		goto done;
	}

	if (unlikely(ctxt->guest_mode) && c->intercept) {
		rc = emulator_check_intercept(ctxt, c->intercept,
					      X86_ICPT_PRE_EXCEPT);
		if (rc != X86EMUL_CONTINUE)
			goto done;
	}

	/* Privileged instruction can be executed only in CPL=0 */
	if ((c->d & Priv) && ops->cpl(ctxt)) {
		rc = emulate_gp(ctxt, 0);
		goto done;
	}

	/* Instruction can only be executed in protected mode */
	if ((c->d & Prot) && !(ctxt->mode & X86EMUL_MODE_PROT)) {
		rc = emulate_ud(ctxt);
		goto done;
	}

	/* Do instruction specific permission checks */
	if (c->check_perm) {
		rc = c->check_perm(ctxt);
		if (rc != X86EMUL_CONTINUE)
			goto done;
	}

	if (unlikely(ctxt->guest_mode) && c->intercept) {
		rc = emulator_check_intercept(ctxt, c->intercept,
					      X86_ICPT_POST_EXCEPT);
		if (rc != X86EMUL_CONTINUE)
			goto done;
	}

	if (c->rep_prefix && (c->d & String)) {
		/* All REP prefixes have the same first termination condition */
		if (address_mask(c, c->regs[VCPU_REGS_RCX]) == 0) {
			ctxt->eip = c->eip;
			goto done;
		}
	}

	if ((c->src.type == OP_MEM) && !(c->d & NoAccess)) {
		rc = segmented_read(ctxt, c->src.addr.mem,
				    c->src.valptr, c->src.bytes);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		c->src.orig_val64 = c->src.val64;
	}

	if (c->src2.type == OP_MEM) {
		rc = segmented_read(ctxt, c->src2.addr.mem,
				    &c->src2.val, c->src2.bytes);
		if (rc != X86EMUL_CONTINUE)
			goto done;
	}

	if ((c->d & DstMask) == ImplicitOps)
		goto special_insn;


	if ((c->dst.type == OP_MEM) && !(c->d & Mov)) {
		/* optimisation - avoid slow emulated read if Mov */
		rc = segmented_read(ctxt, c->dst.addr.mem,
				   &c->dst.val, c->dst.bytes);
		if (rc != X86EMUL_CONTINUE)
			goto done;
	}
	c->dst.orig_val = c->dst.val;

special_insn:

	if (unlikely(ctxt->guest_mode) && c->intercept) {
		rc = emulator_check_intercept(ctxt, c->intercept,
					      X86_ICPT_POST_MEMACCESS);
		if (rc != X86EMUL_CONTINUE)
			goto done;
	}

	if (c->execute) {
		rc = c->execute(ctxt);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		goto writeback;
	}

	if (c->twobyte)
		goto twobyte_insn;

	switch (c->b) {
	case 0x06:		/* push es */
		rc = emulate_push_sreg(ctxt, ops, VCPU_SREG_ES);
		break;
	case 0x07:		/* pop es */
		rc = emulate_pop_sreg(ctxt, ops, VCPU_SREG_ES);
		break;
	case 0x0e:		/* push cs */
		rc = emulate_push_sreg(ctxt, ops, VCPU_SREG_CS);
		break;
	case 0x16:		/* push ss */
		rc = emulate_push_sreg(ctxt, ops, VCPU_SREG_SS);
		break;
	case 0x17:		/* pop ss */
		rc = emulate_pop_sreg(ctxt, ops, VCPU_SREG_SS);
		break;
	case 0x1e:		/* push ds */
		rc = emulate_push_sreg(ctxt, ops, VCPU_SREG_DS);
		break;
	case 0x1f:		/* pop ds */
		rc = emulate_pop_sreg(ctxt, ops, VCPU_SREG_DS);
		break;
	case 0x40 ... 0x47: /* inc r16/r32 */
		emulate_1op("inc", c->dst, ctxt->eflags);
		break;
	case 0x48 ... 0x4f: /* dec r16/r32 */
		emulate_1op("dec", c->dst, ctxt->eflags);
		break;
	case 0x63:		/* movsxd */
		if (ctxt->mode != X86EMUL_MODE_PROT64)
			goto cannot_emulate;
		c->dst.val = (s32) c->src.val;
		break;
	case 0x6c:		/* insb */
	case 0x6d:		/* insw/insd */
		c->src.val = c->regs[VCPU_REGS_RDX];
		goto do_io_in;
	case 0x6e:		/* outsb */
	case 0x6f:		/* outsw/outsd */
		c->dst.val = c->regs[VCPU_REGS_RDX];
		goto do_io_out;
		break;
	case 0x70 ... 0x7f: /* jcc (short) */
		if (test_cc(c->b, ctxt->eflags))
			jmp_rel(c, c->src.val);
		break;
	case 0x84 ... 0x85:
	test:
		emulate_2op_SrcV("test", c->src, c->dst, ctxt->eflags);
		break;
	case 0x86 ... 0x87:	/* xchg */
	xchg:
		/* Write back the register source. */
		c->src.val = c->dst.val;
		write_register_operand(&c->src);
		/*
		 * Write back the memory destination with implicit LOCK
		 * prefix.
		 */
		c->dst.val = c->src.orig_val;
		c->lock_prefix = 1;
		break;
	case 0x8c:  /* mov r/m, sreg */
		if (c->modrm_reg > VCPU_SREG_GS) {
			rc = emulate_ud(ctxt);
			goto done;
		}
		c->dst.val = get_segment_selector(ctxt, c->modrm_reg);
		break;
	case 0x8d: /* lea r16/r32, m */
		c->dst.val = c->src.addr.mem.ea;
		break;
	case 0x8e: { /* mov seg, r/m16 */
		uint16_t sel;

		sel = c->src.val;

		if (c->modrm_reg == VCPU_SREG_CS ||
		    c->modrm_reg > VCPU_SREG_GS) {
			rc = emulate_ud(ctxt);
			goto done;
		}

		if (c->modrm_reg == VCPU_SREG_SS)
			ctxt->interruptibility = KVM_X86_SHADOW_INT_MOV_SS;

		rc = load_segment_descriptor(ctxt, ops, sel, c->modrm_reg);

		c->dst.type = OP_NONE;  /* Disable writeback. */
		break;
	}
	case 0x8f:		/* pop (sole member of Grp1a) */
		rc = em_grp1a(ctxt);
		break;
	case 0x90 ... 0x97: /* nop / xchg reg, rax */
		if (c->dst.addr.reg == &c->regs[VCPU_REGS_RAX])
			break;
		goto xchg;
	case 0x98: /* cbw/cwde/cdqe */
		switch (c->op_bytes) {
		case 2: c->dst.val = (s8)c->dst.val; break;
		case 4: c->dst.val = (s16)c->dst.val; break;
		case 8: c->dst.val = (s32)c->dst.val; break;
		}
		break;
	case 0xa8 ... 0xa9:	/* test ax, imm */
		goto test;
	case 0xc0 ... 0xc1:
		rc = em_grp2(ctxt);
		break;
	case 0xc3: /* ret */
		c->dst.type = OP_REG;
		c->dst.addr.reg = &c->eip;
		c->dst.bytes = c->op_bytes;
		rc = em_pop(ctxt);
		break;
	case 0xc4:		/* les */
		rc = emulate_load_segment(ctxt, ops, VCPU_SREG_ES);
		break;
	case 0xc5:		/* lds */
		rc = emulate_load_segment(ctxt, ops, VCPU_SREG_DS);
		break;
	case 0xcb:		/* ret far */
		rc = emulate_ret_far(ctxt, ops);
		break;
	case 0xcc:		/* int3 */
		irq = 3;
		goto do_interrupt;
	case 0xcd:		/* int n */
		irq = c->src.val;
	do_interrupt:
		rc = emulate_int(ctxt, ops, irq);
		break;
	case 0xce:		/* into */
		if (ctxt->eflags & EFLG_OF) {
			irq = 4;
			goto do_interrupt;
		}
		break;
	case 0xcf:		/* iret */
		rc = emulate_iret(ctxt, ops);
		break;
	case 0xd0 ... 0xd1:	/* Grp2 */
		rc = em_grp2(ctxt);
		break;
	case 0xd2 ... 0xd3:	/* Grp2 */
		c->src.val = c->regs[VCPU_REGS_RCX];
		rc = em_grp2(ctxt);
		break;
	case 0xe0 ... 0xe2:	/* loop/loopz/loopnz */
		register_address_increment(c, &c->regs[VCPU_REGS_RCX], -1);
		if (address_mask(c, c->regs[VCPU_REGS_RCX]) != 0 &&
		    (c->b == 0xe2 || test_cc(c->b ^ 0x5, ctxt->eflags)))
			jmp_rel(c, c->src.val);
		break;
	case 0xe3:	/* jcxz/jecxz/jrcxz */
		if (address_mask(c, c->regs[VCPU_REGS_RCX]) == 0)
			jmp_rel(c, c->src.val);
		break;
	case 0xe4: 	/* inb */
	case 0xe5: 	/* in */
		goto do_io_in;
	case 0xe6: /* outb */
	case 0xe7: /* out */
		goto do_io_out;
	case 0xe8: /* call (near) */ {
		long int rel = c->src.val;
		c->src.val = (unsigned long) c->eip;
		jmp_rel(c, rel);
		rc = em_push(ctxt);
		break;
	}
	case 0xe9: /* jmp rel */
		goto jmp;
	case 0xea: /* jmp far */
		rc = em_jmp_far(ctxt);
		break;
	case 0xeb:
	      jmp:		/* jmp rel short */
		jmp_rel(c, c->src.val);
		c->dst.type = OP_NONE; /* Disable writeback. */
		break;
	case 0xec: /* in al,dx */
	case 0xed: /* in (e/r)ax,dx */
	do_io_in:
		if (!pio_in_emulated(ctxt, ops, c->dst.bytes, c->src.val,
				     &c->dst.val))
			goto done; /* IO is needed */
		break;
	case 0xee: /* out dx,al */
	case 0xef: /* out dx,(e/r)ax */
	do_io_out:
		ops->pio_out_emulated(ctxt, c->src.bytes, c->dst.val,
				      &c->src.val, 1);
		c->dst.type = OP_NONE;	/* Disable writeback. */
		break;
	case 0xf4:              /* hlt */
		ctxt->ops->halt(ctxt);
		break;
	case 0xf5:	/* cmc */
		/* complement carry flag from eflags reg */
		ctxt->eflags ^= EFLG_CF;
		break;
	case 0xf6 ... 0xf7:	/* Grp3 */
		rc = em_grp3(ctxt);
		break;
	case 0xf8: /* clc */
		ctxt->eflags &= ~EFLG_CF;
		break;
	case 0xf9: /* stc */
		ctxt->eflags |= EFLG_CF;
		break;
	case 0xfa: /* cli */
		if (emulator_bad_iopl(ctxt, ops)) {
			rc = emulate_gp(ctxt, 0);
			goto done;
		} else
			ctxt->eflags &= ~X86_EFLAGS_IF;
		break;
	case 0xfb: /* sti */
		if (emulator_bad_iopl(ctxt, ops)) {
			rc = emulate_gp(ctxt, 0);
			goto done;
		} else {
			ctxt->interruptibility = KVM_X86_SHADOW_INT_STI;
			ctxt->eflags |= X86_EFLAGS_IF;
		}
		break;
	case 0xfc: /* cld */
		ctxt->eflags &= ~EFLG_DF;
		break;
	case 0xfd: /* std */
		ctxt->eflags |= EFLG_DF;
		break;
	case 0xfe: /* Grp4 */
		rc = em_grp45(ctxt);
		break;
	case 0xff: /* Grp5 */
		rc = em_grp45(ctxt);
		break;
	default:
		goto cannot_emulate;
	}

	if (rc != X86EMUL_CONTINUE)
		goto done;

writeback:
	rc = writeback(ctxt);
	if (rc != X86EMUL_CONTINUE)
		goto done;

	/*
	 * restore dst type in case the decoding will be reused
	 * (happens for string instruction )
	 */
	c->dst.type = saved_dst_type;

	if ((c->d & SrcMask) == SrcSI)
		string_addr_inc(ctxt, seg_override(ctxt, c),
				VCPU_REGS_RSI, &c->src);

	if ((c->d & DstMask) == DstDI)
		string_addr_inc(ctxt, VCPU_SREG_ES, VCPU_REGS_RDI,
				&c->dst);

	if (c->rep_prefix && (c->d & String)) {
		struct read_cache *r = &ctxt->decode.io_read;
		register_address_increment(c, &c->regs[VCPU_REGS_RCX], -1);

		if (!string_insn_completed(ctxt)) {
			/*
			 * Re-enter guest when pio read ahead buffer is empty
			 * or, if it is not used, after each 1024 iteration.
			 */
			if ((r->end != 0 || c->regs[VCPU_REGS_RCX] & 0x3ff) &&
			    (r->end == 0 || r->end != r->pos)) {
				/*
				 * Reset read cache. Usually happens before
				 * decode, but since instruction is restarted
				 * we have to do it here.
				 */
				ctxt->decode.mem_read.end = 0;
				return EMULATION_RESTART;
			}
			goto done; /* skip rip writeback */
		}
	}

	ctxt->eip = c->eip;

done:
	if (rc == X86EMUL_PROPAGATE_FAULT)
		ctxt->have_exception = true;
	if (rc == X86EMUL_INTERCEPTED)
		return EMULATION_INTERCEPTED;

	return (rc == X86EMUL_UNHANDLEABLE) ? EMULATION_FAILED : EMULATION_OK;

twobyte_insn:
	switch (c->b) {
	case 0x05: 		/* syscall */
		rc = emulate_syscall(ctxt, ops);
		break;
	case 0x06:
		rc = em_clts(ctxt);
		break;
	case 0x09:		/* wbinvd */
		(ctxt->ops->wbinvd)(ctxt);
		break;
	case 0x08:		/* invd */
	case 0x0d:		/* GrpP (prefetch) */
	case 0x18:		/* Grp16 (prefetch/nop) */
		break;
	case 0x20: /* mov cr, reg */
		c->dst.val = ops->get_cr(ctxt, c->modrm_reg);
		break;
	case 0x21: /* mov from dr to reg */
		ops->get_dr(ctxt, c->modrm_reg, &c->dst.val);
		break;
	case 0x22: /* mov reg, cr */
		if (ops->set_cr(ctxt, c->modrm_reg, c->src.val)) {
			emulate_gp(ctxt, 0);
			rc = X86EMUL_PROPAGATE_FAULT;
			goto done;
		}
		c->dst.type = OP_NONE;
		break;
	case 0x23: /* mov from reg to dr */
		if (ops->set_dr(ctxt, c->modrm_reg, c->src.val &
				((ctxt->mode == X86EMUL_MODE_PROT64) ?
				 ~0ULL : ~0U)) < 0) {
			/* #UD condition is already handled by the code above */
			emulate_gp(ctxt, 0);
			rc = X86EMUL_PROPAGATE_FAULT;
			goto done;
		}

		c->dst.type = OP_NONE;	/* no writeback */
		break;
	case 0x30:
		/* wrmsr */
		msr_data = (u32)c->regs[VCPU_REGS_RAX]
			| ((u64)c->regs[VCPU_REGS_RDX] << 32);
		if (ops->set_msr(ctxt, c->regs[VCPU_REGS_RCX], msr_data)) {
			emulate_gp(ctxt, 0);
			rc = X86EMUL_PROPAGATE_FAULT;
			goto done;
		}
		rc = X86EMUL_CONTINUE;
		break;
	case 0x32:
		/* rdmsr */
		if (ops->get_msr(ctxt, c->regs[VCPU_REGS_RCX], &msr_data)) {
			emulate_gp(ctxt, 0);
			rc = X86EMUL_PROPAGATE_FAULT;
			goto done;
		} else {
			c->regs[VCPU_REGS_RAX] = (u32)msr_data;
			c->regs[VCPU_REGS_RDX] = msr_data >> 32;
		}
		rc = X86EMUL_CONTINUE;
		break;
	case 0x34:		/* sysenter */
		rc = emulate_sysenter(ctxt, ops);
		break;
	case 0x35:		/* sysexit */
		rc = emulate_sysexit(ctxt, ops);
		break;
	case 0x40 ... 0x4f:	/* cmov */
		c->dst.val = c->dst.orig_val = c->src.val;
		if (!test_cc(c->b, ctxt->eflags))
			c->dst.type = OP_NONE; /* no writeback */
		break;
	case 0x80 ... 0x8f: /* jnz rel, etc*/
		if (test_cc(c->b, ctxt->eflags))
			jmp_rel(c, c->src.val);
		break;
	case 0x90 ... 0x9f:     /* setcc r/m8 */
		c->dst.val = test_cc(c->b, ctxt->eflags);
		break;
	case 0xa0:	  /* push fs */
		rc = emulate_push_sreg(ctxt, ops, VCPU_SREG_FS);
		break;
	case 0xa1:	 /* pop fs */
		rc = emulate_pop_sreg(ctxt, ops, VCPU_SREG_FS);
		break;
	case 0xa3:
	      bt:		/* bt */
		c->dst.type = OP_NONE;
		/* only subword offset */
		c->src.val &= (c->dst.bytes << 3) - 1;
		emulate_2op_SrcV_nobyte("bt", c->src, c->dst, ctxt->eflags);
		break;
	case 0xa4: /* shld imm8, r, r/m */
	case 0xa5: /* shld cl, r, r/m */
		emulate_2op_cl("shld", c->src2, c->src, c->dst, ctxt->eflags);
		break;
	case 0xa8:	/* push gs */
		rc = emulate_push_sreg(ctxt, ops, VCPU_SREG_GS);
		break;
	case 0xa9:	/* pop gs */
		rc = emulate_pop_sreg(ctxt, ops, VCPU_SREG_GS);
		break;
	case 0xab:
	      bts:		/* bts */
		emulate_2op_SrcV_nobyte("bts", c->src, c->dst, ctxt->eflags);
		break;
	case 0xac: /* shrd imm8, r, r/m */
	case 0xad: /* shrd cl, r, r/m */
		emulate_2op_cl("shrd", c->src2, c->src, c->dst, ctxt->eflags);
		break;
	case 0xae:              /* clflush */
		break;
	case 0xb0 ... 0xb1:	/* cmpxchg */
		/*
		 * Save real source value, then compare EAX against
		 * destination.
		 */
		c->src.orig_val = c->src.val;
		c->src.val = c->regs[VCPU_REGS_RAX];
		emulate_2op_SrcV("cmp", c->src, c->dst, ctxt->eflags);
		if (ctxt->eflags & EFLG_ZF) {
			/* Success: write back to memory. */
			c->dst.val = c->src.orig_val;
		} else {
			/* Failure: write the value we saw to EAX. */
			c->dst.type = OP_REG;
			c->dst.addr.reg = (unsigned long *)&c->regs[VCPU_REGS_RAX];
		}
		break;
	case 0xb2:		/* lss */
		rc = emulate_load_segment(ctxt, ops, VCPU_SREG_SS);
		break;
	case 0xb3:
	      btr:		/* btr */
		emulate_2op_SrcV_nobyte("btr", c->src, c->dst, ctxt->eflags);
		break;
	case 0xb4:		/* lfs */
		rc = emulate_load_segment(ctxt, ops, VCPU_SREG_FS);
		break;
	case 0xb5:		/* lgs */
		rc = emulate_load_segment(ctxt, ops, VCPU_SREG_GS);
		break;
	case 0xb6 ... 0xb7:	/* movzx */
		c->dst.bytes = c->op_bytes;
		c->dst.val = (c->d & ByteOp) ? (u8) c->src.val
						       : (u16) c->src.val;
		break;
	case 0xba:		/* Grp8 */
		switch (c->modrm_reg & 3) {
		case 0:
			goto bt;
		case 1:
			goto bts;
		case 2:
			goto btr;
		case 3:
			goto btc;
		}
		break;
	case 0xbb:
	      btc:		/* btc */
		emulate_2op_SrcV_nobyte("btc", c->src, c->dst, ctxt->eflags);
		break;
	case 0xbc: {		/* bsf */
		u8 zf;
		__asm__ ("bsf %2, %0; setz %1"
			 : "=r"(c->dst.val), "=q"(zf)
			 : "r"(c->src.val));
		ctxt->eflags &= ~X86_EFLAGS_ZF;
		if (zf) {
			ctxt->eflags |= X86_EFLAGS_ZF;
			c->dst.type = OP_NONE;	/* Disable writeback. */
		}
		break;
	}
	case 0xbd: {		/* bsr */
		u8 zf;
		__asm__ ("bsr %2, %0; setz %1"
			 : "=r"(c->dst.val), "=q"(zf)
			 : "r"(c->src.val));
		ctxt->eflags &= ~X86_EFLAGS_ZF;
		if (zf) {
			ctxt->eflags |= X86_EFLAGS_ZF;
			c->dst.type = OP_NONE;	/* Disable writeback. */
		}
		break;
	}
	case 0xbe ... 0xbf:	/* movsx */
		c->dst.bytes = c->op_bytes;
		c->dst.val = (c->d & ByteOp) ? (s8) c->src.val :
							(s16) c->src.val;
		break;
	case 0xc0 ... 0xc1:	/* xadd */
		emulate_2op_SrcV("add", c->src, c->dst, ctxt->eflags);
		/* Write back the register source. */
		c->src.val = c->dst.orig_val;
		write_register_operand(&c->src);
		break;
	case 0xc3:		/* movnti */
		c->dst.bytes = c->op_bytes;
		c->dst.val = (c->op_bytes == 4) ? (u32) c->src.val :
							(u64) c->src.val;
		break;
	case 0xc7:		/* Grp9 (cmpxchg8b) */
		rc = em_grp9(ctxt);
		break;
	default:
		goto cannot_emulate;
	}

	if (rc != X86EMUL_CONTINUE)
		goto done;

	goto writeback;

cannot_emulate:
	return EMULATION_FAILED;
}
