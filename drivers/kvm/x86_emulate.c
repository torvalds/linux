/******************************************************************************
 * x86_emulate.c
 *
 * Generic x86 (32-bit and 64-bit) instruction decoder and emulator.
 *
 * Copyright (c) 2005 Keir Fraser
 *
 * Linux coding style, mod r/m decoder, segment base fixes, real-mode
 * privileged instructions:
 *
 * Copyright (C) 2006 Qumranet
 *
 *   Avi Kivity <avi@qumranet.com>
 *   Yaniv Kamay <yaniv@qumranet.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * From: xen-unstable 10676:af9809f51f81a3c43f276f00c81a52ef558afda4
 */

#ifndef __KERNEL__
#include <stdio.h>
#include <stdint.h>
#include <public/xen.h>
#define DPRINTF(_f, _a ...) printf( _f , ## _a )
#else
#include "kvm.h"
#define DPRINTF(x...) do {} while (0)
#endif
#include "x86_emulate.h"
#include <linux/module.h>

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
#define DstMask     (3<<1)
/* Source operand type. */
#define SrcNone     (0<<3)	/* No source operand. */
#define SrcImplicit (0<<3)	/* Source operand is implicit in the opcode. */
#define SrcReg      (1<<3)	/* Register operand. */
#define SrcMem      (2<<3)	/* Memory operand. */
#define SrcMem16    (3<<3)	/* Memory operand (16-bit). */
#define SrcMem32    (4<<3)	/* Memory operand (32-bit). */
#define SrcImm      (5<<3)	/* Immediate operand. */
#define SrcImmByte  (6<<3)	/* 8-bit sign-extended immediate operand. */
#define SrcMask     (7<<3)
/* Generic ModRM decode. */
#define ModRM       (1<<6)
/* Destination is only written; never read. */
#define Mov         (1<<7)
#define BitOp       (1<<8)

static u8 opcode_table[256] = {
	/* 0x00 - 0x07 */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	0, 0, 0, 0,
	/* 0x08 - 0x0F */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	0, 0, 0, 0,
	/* 0x10 - 0x17 */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	0, 0, 0, 0,
	/* 0x18 - 0x1F */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	0, 0, 0, 0,
	/* 0x20 - 0x27 */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	SrcImmByte, SrcImm, 0, 0,
	/* 0x28 - 0x2F */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	0, 0, 0, 0,
	/* 0x30 - 0x37 */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	0, 0, 0, 0,
	/* 0x38 - 0x3F */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	0, 0, 0, 0,
	/* 0x40 - 0x4F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x50 - 0x57 */
	ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
	ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
	/* 0x58 - 0x5F */
	ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
	ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
	/* 0x60 - 0x67 */
	0, 0, 0, DstReg | SrcMem32 | ModRM | Mov /* movsxd (x86/64) */ ,
	0, 0, 0, 0,
	/* 0x68 - 0x6F */
	0, 0, ImplicitOps|Mov, 0,
	SrcNone  | ByteOp  | ImplicitOps, SrcNone  | ImplicitOps, /* insb, insw/insd */
	SrcNone  | ByteOp  | ImplicitOps, SrcNone  | ImplicitOps, /* outsb, outsw/outsd */
	/* 0x70 - 0x77 */
	ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
	ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
	/* 0x78 - 0x7F */
	ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
	ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
	/* 0x80 - 0x87 */
	ByteOp | DstMem | SrcImm | ModRM, DstMem | SrcImm | ModRM,
	ByteOp | DstMem | SrcImm | ModRM, DstMem | SrcImmByte | ModRM,
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	/* 0x88 - 0x8F */
	ByteOp | DstMem | SrcReg | ModRM | Mov, DstMem | SrcReg | ModRM | Mov,
	ByteOp | DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	0, ModRM | DstReg, 0, DstMem | SrcNone | ModRM | Mov,
	/* 0x90 - 0x9F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, ImplicitOps, ImplicitOps, 0, 0,
	/* 0xA0 - 0xA7 */
	ByteOp | DstReg | SrcMem | Mov, DstReg | SrcMem | Mov,
	ByteOp | DstMem | SrcReg | Mov, DstMem | SrcReg | Mov,
	ByteOp | ImplicitOps | Mov, ImplicitOps | Mov,
	ByteOp | ImplicitOps, ImplicitOps,
	/* 0xA8 - 0xAF */
	0, 0, ByteOp | ImplicitOps | Mov, ImplicitOps | Mov,
	ByteOp | ImplicitOps | Mov, ImplicitOps | Mov,
	ByteOp | ImplicitOps, ImplicitOps,
	/* 0xB0 - 0xBF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xC0 - 0xC7 */
	ByteOp | DstMem | SrcImm | ModRM, DstMem | SrcImmByte | ModRM,
	0, ImplicitOps, 0, 0,
	ByteOp | DstMem | SrcImm | ModRM | Mov, DstMem | SrcImm | ModRM | Mov,
	/* 0xC8 - 0xCF */
	0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xD0 - 0xD7 */
	ByteOp | DstMem | SrcImplicit | ModRM, DstMem | SrcImplicit | ModRM,
	ByteOp | DstMem | SrcImplicit | ModRM, DstMem | SrcImplicit | ModRM,
	0, 0, 0, 0,
	/* 0xD8 - 0xDF */
	0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xE0 - 0xE7 */
	0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xE8 - 0xEF */
	ImplicitOps, SrcImm|ImplicitOps, 0, SrcImmByte|ImplicitOps, 0, 0, 0, 0,
	/* 0xF0 - 0xF7 */
	0, 0, 0, 0,
	ImplicitOps, 0,
	ByteOp | DstMem | SrcNone | ModRM, DstMem | SrcNone | ModRM,
	/* 0xF8 - 0xFF */
	0, 0, 0, 0,
	0, 0, ByteOp | DstMem | SrcNone | ModRM, DstMem | SrcNone | ModRM
};

static u16 twobyte_table[256] = {
	/* 0x00 - 0x0F */
	0, SrcMem | ModRM | DstReg, 0, 0, 0, 0, ImplicitOps, 0,
	0, ImplicitOps, 0, 0, 0, ImplicitOps | ModRM, 0, 0,
	/* 0x10 - 0x1F */
	0, 0, 0, 0, 0, 0, 0, 0, ImplicitOps | ModRM, 0, 0, 0, 0, 0, 0, 0,
	/* 0x20 - 0x2F */
	ModRM | ImplicitOps, ModRM, ModRM | ImplicitOps, ModRM, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x30 - 0x3F */
	ImplicitOps, 0, ImplicitOps, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x40 - 0x47 */
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	/* 0x48 - 0x4F */
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	/* 0x50 - 0x5F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x60 - 0x6F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x70 - 0x7F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x80 - 0x8F */
	ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
	ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
	ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
	ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
	/* 0x90 - 0x9F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xA0 - 0xA7 */
	0, 0, 0, DstMem | SrcReg | ModRM | BitOp, 0, 0, 0, 0,
	/* 0xA8 - 0xAF */
	0, 0, 0, DstMem | SrcReg | ModRM | BitOp, 0, 0, 0, 0,
	/* 0xB0 - 0xB7 */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM, 0,
	    DstMem | SrcReg | ModRM | BitOp,
	0, 0, ByteOp | DstReg | SrcMem | ModRM | Mov,
	    DstReg | SrcMem16 | ModRM | Mov,
	/* 0xB8 - 0xBF */
	0, 0, DstMem | SrcImmByte | ModRM, DstMem | SrcReg | ModRM | BitOp,
	0, 0, ByteOp | DstReg | SrcMem | ModRM | Mov,
	    DstReg | SrcMem16 | ModRM | Mov,
	/* 0xC0 - 0xCF */
	0, 0, 0, DstMem | SrcReg | ModRM | Mov, 0, 0, 0, ImplicitOps | ModRM,
	0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xD0 - 0xDF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xE0 - 0xEF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xF0 - 0xFF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Type, address-of, and value of an instruction's operand. */
struct operand {
	enum { OP_REG, OP_MEM, OP_IMM } type;
	unsigned int bytes;
	unsigned long val, orig_val, *ptr;
};

/* EFLAGS bit definitions. */
#define EFLG_OF (1<<11)
#define EFLG_DF (1<<10)
#define EFLG_SF (1<<7)
#define EFLG_ZF (1<<6)
#define EFLG_AF (1<<4)
#define EFLG_PF (1<<2)
#define EFLG_CF (1<<0)

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
#define _PRE_EFLAGS(_sav, _msk, _tmp) \
	/* EFLAGS = (_sav & _msk) | (EFLAGS & ~_msk); */	\
	"push %"_sav"; "					\
	"movl %"_msk",%"_LO32 _tmp"; "				\
	"andl %"_LO32 _tmp",("_STK"); "				\
	"pushf; "						\
	"notl %"_LO32 _tmp"; "					\
	"andl %"_LO32 _tmp",("_STK"); "				\
	"pop  %"_tmp"; "					\
	"orl  %"_LO32 _tmp",("_STK"); "				\
	"popf; "						\
	/* _sav &= ~msk; */					\
	"movl %"_msk",%"_LO32 _tmp"; "				\
	"notl %"_LO32 _tmp"; "					\
	"andl %"_LO32 _tmp",%"_sav"; "

/* After executing instruction: write-back necessary bits in EFLAGS. */
#define _POST_EFLAGS(_sav, _msk, _tmp) \
	/* _sav |= EFLAGS & _msk; */		\
	"pushf; "				\
	"pop  %"_tmp"; "			\
	"andl %"_msk",%"_LO32 _tmp"; "		\
	"orl  %"_LO32 _tmp",%"_sav"; "

/* Raw emulation: instruction has two explicit operands. */
#define __emulate_2op_nobyte(_op,_src,_dst,_eflags,_wx,_wy,_lx,_ly,_qx,_qy) \
	do { 								    \
		unsigned long _tmp;					    \
									    \
		switch ((_dst).bytes) {					    \
		case 2:							    \
			__asm__ __volatile__ (				    \
				_PRE_EFLAGS("0","4","2")		    \
				_op"w %"_wx"3,%1; "			    \
				_POST_EFLAGS("0","4","2")		    \
				: "=m" (_eflags), "=m" ((_dst).val),        \
				  "=&r" (_tmp)				    \
				: _wy ((_src).val), "i" (EFLAGS_MASK) );    \
			break;						    \
		case 4:							    \
			__asm__ __volatile__ (				    \
				_PRE_EFLAGS("0","4","2")		    \
				_op"l %"_lx"3,%1; "			    \
				_POST_EFLAGS("0","4","2")		    \
				: "=m" (_eflags), "=m" ((_dst).val),	    \
				  "=&r" (_tmp)				    \
				: _ly ((_src).val), "i" (EFLAGS_MASK) );    \
			break;						    \
		case 8:							    \
			__emulate_2op_8byte(_op, _src, _dst,		    \
					    _eflags, _qx, _qy);		    \
			break;						    \
		}							    \
	} while (0)

#define __emulate_2op(_op,_src,_dst,_eflags,_bx,_by,_wx,_wy,_lx,_ly,_qx,_qy) \
	do {								     \
		unsigned long _tmp;					     \
		switch ( (_dst).bytes )					     \
		{							     \
		case 1:							     \
			__asm__ __volatile__ (				     \
				_PRE_EFLAGS("0","4","2")		     \
				_op"b %"_bx"3,%1; "			     \
				_POST_EFLAGS("0","4","2")		     \
				: "=m" (_eflags), "=m" ((_dst).val),	     \
				  "=&r" (_tmp)				     \
				: _by ((_src).val), "i" (EFLAGS_MASK) );     \
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

/* Instruction has only one explicit operand (no source operand). */
#define emulate_1op(_op, _dst, _eflags)                                    \
	do {								\
		unsigned long _tmp;					\
									\
		switch ( (_dst).bytes )					\
		{							\
		case 1:							\
			__asm__ __volatile__ (				\
				_PRE_EFLAGS("0","3","2")		\
				_op"b %1; "				\
				_POST_EFLAGS("0","3","2")		\
				: "=m" (_eflags), "=m" ((_dst).val),	\
				  "=&r" (_tmp)				\
				: "i" (EFLAGS_MASK) );			\
			break;						\
		case 2:							\
			__asm__ __volatile__ (				\
				_PRE_EFLAGS("0","3","2")		\
				_op"w %1; "				\
				_POST_EFLAGS("0","3","2")		\
				: "=m" (_eflags), "=m" ((_dst).val),	\
				  "=&r" (_tmp)				\
				: "i" (EFLAGS_MASK) );			\
			break;						\
		case 4:							\
			__asm__ __volatile__ (				\
				_PRE_EFLAGS("0","3","2")		\
				_op"l %1; "				\
				_POST_EFLAGS("0","3","2")		\
				: "=m" (_eflags), "=m" ((_dst).val),	\
				  "=&r" (_tmp)				\
				: "i" (EFLAGS_MASK) );			\
			break;						\
		case 8:							\
			__emulate_1op_8byte(_op, _dst, _eflags);	\
			break;						\
		}							\
	} while (0)

/* Emulate an instruction with quadword operands (x86/64 only). */
#if defined(CONFIG_X86_64)
#define __emulate_2op_8byte(_op, _src, _dst, _eflags, _qx, _qy)           \
	do {								  \
		__asm__ __volatile__ (					  \
			_PRE_EFLAGS("0","4","2")			  \
			_op"q %"_qx"3,%1; "				  \
			_POST_EFLAGS("0","4","2")			  \
			: "=m" (_eflags), "=m" ((_dst).val), "=&r" (_tmp) \
			: _qy ((_src).val), "i" (EFLAGS_MASK) );	  \
	} while (0)

#define __emulate_1op_8byte(_op, _dst, _eflags)                           \
	do {								  \
		__asm__ __volatile__ (					  \
			_PRE_EFLAGS("0","3","2")			  \
			_op"q %1; "					  \
			_POST_EFLAGS("0","3","2")			  \
			: "=m" (_eflags), "=m" ((_dst).val), "=&r" (_tmp) \
			: "i" (EFLAGS_MASK) );				  \
	} while (0)

#elif defined(__i386__)
#define __emulate_2op_8byte(_op, _src, _dst, _eflags, _qx, _qy)
#define __emulate_1op_8byte(_op, _dst, _eflags)
#endif				/* __i386__ */

/* Fetch next part of the instruction being emulated. */
#define insn_fetch(_type, _size, _eip)                                  \
({	unsigned long _x;						\
	rc = ops->read_std((unsigned long)(_eip) + ctxt->cs_base, &_x,	\
                                                  (_size), ctxt->vcpu); \
	if ( rc != 0 )							\
		goto done;						\
	(_eip) += (_size);						\
	(_type)_x;							\
})

/* Access/update address held in a register, based on addressing mode. */
#define address_mask(reg)						\
	((ad_bytes == sizeof(unsigned long)) ? 				\
		(reg) :	((reg) & ((1UL << (ad_bytes << 3)) - 1)))
#define register_address(base, reg)                                     \
	((base) + address_mask(reg))
#define register_address_increment(reg, inc)                            \
	do {								\
		/* signed type ensures sign extension to long */        \
		int _inc = (inc);					\
		if ( ad_bytes == sizeof(unsigned long) )		\
			(reg) += _inc;					\
		else							\
			(reg) = ((reg) & ~((1UL << (ad_bytes << 3)) - 1)) | \
			   (((reg) + _inc) & ((1UL << (ad_bytes << 3)) - 1)); \
	} while (0)

#define JMP_REL(rel) 							\
	do {								\
		_eip += (int)(rel);					\
		_eip = ((op_bytes == 2) ? (uint16_t)_eip : (uint32_t)_eip); \
	} while (0)

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
			   struct x86_emulate_ops *ops,
			   void *ptr,
			   u16 *size, unsigned long *address, int op_bytes)
{
	int rc;

	if (op_bytes == 2)
		op_bytes = 3;
	*address = 0;
	rc = ops->read_std((unsigned long)ptr, (unsigned long *)size, 2,
			   ctxt->vcpu);
	if (rc)
		return rc;
	rc = ops->read_std((unsigned long)ptr + 2, address, op_bytes,
			   ctxt->vcpu);
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

int
x86_emulate_memop(struct x86_emulate_ctxt *ctxt, struct x86_emulate_ops *ops)
{
	unsigned d;
	u8 b, sib, twobyte = 0, rex_prefix = 0;
	u8 modrm, modrm_mod = 0, modrm_reg = 0, modrm_rm = 0;
	unsigned long *override_base = NULL;
	unsigned int op_bytes, ad_bytes, lock_prefix = 0, rep_prefix = 0, i;
	int rc = 0;
	struct operand src, dst;
	unsigned long cr2 = ctxt->cr2;
	int mode = ctxt->mode;
	unsigned long modrm_ea;
	int use_modrm_ea, index_reg = 0, base_reg = 0, scale, rip_relative = 0;
	int no_wb = 0;
	u64 msr_data;

	/* Shadow copy of register state. Committed on successful emulation. */
	unsigned long _regs[NR_VCPU_REGS];
	unsigned long _eip = ctxt->vcpu->rip, _eflags = ctxt->eflags;
	unsigned long modrm_val = 0;

	memcpy(_regs, ctxt->vcpu->regs, sizeof _regs);

	switch (mode) {
	case X86EMUL_MODE_REAL:
	case X86EMUL_MODE_PROT16:
		op_bytes = ad_bytes = 2;
		break;
	case X86EMUL_MODE_PROT32:
		op_bytes = ad_bytes = 4;
		break;
#ifdef CONFIG_X86_64
	case X86EMUL_MODE_PROT64:
		op_bytes = 4;
		ad_bytes = 8;
		break;
#endif
	default:
		return -1;
	}

	/* Legacy prefixes. */
	for (i = 0; i < 8; i++) {
		switch (b = insn_fetch(u8, 1, _eip)) {
		case 0x66:	/* operand-size override */
			op_bytes ^= 6;	/* switch between 2/4 bytes */
			break;
		case 0x67:	/* address-size override */
			if (mode == X86EMUL_MODE_PROT64)
				ad_bytes ^= 12;	/* switch between 4/8 bytes */
			else
				ad_bytes ^= 6;	/* switch between 2/4 bytes */
			break;
		case 0x2e:	/* CS override */
			override_base = &ctxt->cs_base;
			break;
		case 0x3e:	/* DS override */
			override_base = &ctxt->ds_base;
			break;
		case 0x26:	/* ES override */
			override_base = &ctxt->es_base;
			break;
		case 0x64:	/* FS override */
			override_base = &ctxt->fs_base;
			break;
		case 0x65:	/* GS override */
			override_base = &ctxt->gs_base;
			break;
		case 0x36:	/* SS override */
			override_base = &ctxt->ss_base;
			break;
		case 0xf0:	/* LOCK */
			lock_prefix = 1;
			break;
		case 0xf2:	/* REPNE/REPNZ */
		case 0xf3:	/* REP/REPE/REPZ */
			rep_prefix = 1;
			break;
		default:
			goto done_prefixes;
		}
	}

done_prefixes:

	/* REX prefix. */
	if ((mode == X86EMUL_MODE_PROT64) && ((b & 0xf0) == 0x40)) {
		rex_prefix = b;
		if (b & 8)
			op_bytes = 8;	/* REX.W */
		modrm_reg = (b & 4) << 1;	/* REX.R */
		index_reg = (b & 2) << 2; /* REX.X */
		modrm_rm = base_reg = (b & 1) << 3; /* REG.B */
		b = insn_fetch(u8, 1, _eip);
	}

	/* Opcode byte(s). */
	d = opcode_table[b];
	if (d == 0) {
		/* Two-byte opcode? */
		if (b == 0x0f) {
			twobyte = 1;
			b = insn_fetch(u8, 1, _eip);
			d = twobyte_table[b];
		}

		/* Unrecognised? */
		if (d == 0)
			goto cannot_emulate;
	}

	/* ModRM and SIB bytes. */
	if (d & ModRM) {
		modrm = insn_fetch(u8, 1, _eip);
		modrm_mod |= (modrm & 0xc0) >> 6;
		modrm_reg |= (modrm & 0x38) >> 3;
		modrm_rm |= (modrm & 0x07);
		modrm_ea = 0;
		use_modrm_ea = 1;

		if (modrm_mod == 3) {
			modrm_val = *(unsigned long *)
				decode_register(modrm_rm, _regs, d & ByteOp);
			goto modrm_done;
		}

		if (ad_bytes == 2) {
			unsigned bx = _regs[VCPU_REGS_RBX];
			unsigned bp = _regs[VCPU_REGS_RBP];
			unsigned si = _regs[VCPU_REGS_RSI];
			unsigned di = _regs[VCPU_REGS_RDI];

			/* 16-bit ModR/M decode. */
			switch (modrm_mod) {
			case 0:
				if (modrm_rm == 6)
					modrm_ea += insn_fetch(u16, 2, _eip);
				break;
			case 1:
				modrm_ea += insn_fetch(s8, 1, _eip);
				break;
			case 2:
				modrm_ea += insn_fetch(u16, 2, _eip);
				break;
			}
			switch (modrm_rm) {
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
				if (modrm_mod != 0)
					modrm_ea += bp;
				break;
			case 7:
				modrm_ea += bx;
				break;
			}
			if (modrm_rm == 2 || modrm_rm == 3 ||
			    (modrm_rm == 6 && modrm_mod != 0))
				if (!override_base)
					override_base = &ctxt->ss_base;
			modrm_ea = (u16)modrm_ea;
		} else {
			/* 32/64-bit ModR/M decode. */
			switch (modrm_rm) {
			case 4:
			case 12:
				sib = insn_fetch(u8, 1, _eip);
				index_reg |= (sib >> 3) & 7;
				base_reg |= sib & 7;
				scale = sib >> 6;

				switch (base_reg) {
				case 5:
					if (modrm_mod != 0)
						modrm_ea += _regs[base_reg];
					else
						modrm_ea += insn_fetch(s32, 4, _eip);
					break;
				default:
					modrm_ea += _regs[base_reg];
				}
				switch (index_reg) {
				case 4:
					break;
				default:
					modrm_ea += _regs[index_reg] << scale;

				}
				break;
			case 5:
				if (modrm_mod != 0)
					modrm_ea += _regs[modrm_rm];
				else if (mode == X86EMUL_MODE_PROT64)
					rip_relative = 1;
				break;
			default:
				modrm_ea += _regs[modrm_rm];
				break;
			}
			switch (modrm_mod) {
			case 0:
				if (modrm_rm == 5)
					modrm_ea += insn_fetch(s32, 4, _eip);
				break;
			case 1:
				modrm_ea += insn_fetch(s8, 1, _eip);
				break;
			case 2:
				modrm_ea += insn_fetch(s32, 4, _eip);
				break;
			}
		}
		if (!override_base)
			override_base = &ctxt->ds_base;
		if (mode == X86EMUL_MODE_PROT64 &&
		    override_base != &ctxt->fs_base &&
		    override_base != &ctxt->gs_base)
			override_base = NULL;

		if (override_base)
			modrm_ea += *override_base;

		if (rip_relative) {
			modrm_ea += _eip;
			switch (d & SrcMask) {
			case SrcImmByte:
				modrm_ea += 1;
				break;
			case SrcImm:
				if (d & ByteOp)
					modrm_ea += 1;
				else
					if (op_bytes == 8)
						modrm_ea += 4;
					else
						modrm_ea += op_bytes;
			}
		}
		if (ad_bytes != 8)
			modrm_ea = (u32)modrm_ea;
		cr2 = modrm_ea;
	modrm_done:
		;
	}

	/*
	 * Decode and fetch the source operand: register, memory
	 * or immediate.
	 */
	switch (d & SrcMask) {
	case SrcNone:
		break;
	case SrcReg:
		src.type = OP_REG;
		if (d & ByteOp) {
			src.ptr = decode_register(modrm_reg, _regs,
						  (rex_prefix == 0));
			src.val = src.orig_val = *(u8 *) src.ptr;
			src.bytes = 1;
		} else {
			src.ptr = decode_register(modrm_reg, _regs, 0);
			switch ((src.bytes = op_bytes)) {
			case 2:
				src.val = src.orig_val = *(u16 *) src.ptr;
				break;
			case 4:
				src.val = src.orig_val = *(u32 *) src.ptr;
				break;
			case 8:
				src.val = src.orig_val = *(u64 *) src.ptr;
				break;
			}
		}
		break;
	case SrcMem16:
		src.bytes = 2;
		goto srcmem_common;
	case SrcMem32:
		src.bytes = 4;
		goto srcmem_common;
	case SrcMem:
		src.bytes = (d & ByteOp) ? 1 : op_bytes;
		/* Don't fetch the address for invlpg: it could be unmapped. */
		if (twobyte && b == 0x01 && modrm_reg == 7)
			break;
	      srcmem_common:
		/*
		 * For instructions with a ModR/M byte, switch to register
		 * access if Mod = 3.
		 */
		if ((d & ModRM) && modrm_mod == 3) {
			src.type = OP_REG;
			break;
		}
		src.type = OP_MEM;
		src.ptr = (unsigned long *)cr2;
		src.val = 0;
		if ((rc = ops->read_emulated((unsigned long)src.ptr,
					     &src.val, src.bytes, ctxt->vcpu)) != 0)
			goto done;
		src.orig_val = src.val;
		break;
	case SrcImm:
		src.type = OP_IMM;
		src.ptr = (unsigned long *)_eip;
		src.bytes = (d & ByteOp) ? 1 : op_bytes;
		if (src.bytes == 8)
			src.bytes = 4;
		/* NB. Immediates are sign-extended as necessary. */
		switch (src.bytes) {
		case 1:
			src.val = insn_fetch(s8, 1, _eip);
			break;
		case 2:
			src.val = insn_fetch(s16, 2, _eip);
			break;
		case 4:
			src.val = insn_fetch(s32, 4, _eip);
			break;
		}
		break;
	case SrcImmByte:
		src.type = OP_IMM;
		src.ptr = (unsigned long *)_eip;
		src.bytes = 1;
		src.val = insn_fetch(s8, 1, _eip);
		break;
	}

	/* Decode and fetch the destination operand: register or memory. */
	switch (d & DstMask) {
	case ImplicitOps:
		/* Special instructions do their own operand decoding. */
		goto special_insn;
	case DstReg:
		dst.type = OP_REG;
		if ((d & ByteOp)
		    && !(twobyte && (b == 0xb6 || b == 0xb7))) {
			dst.ptr = decode_register(modrm_reg, _regs,
						  (rex_prefix == 0));
			dst.val = *(u8 *) dst.ptr;
			dst.bytes = 1;
		} else {
			dst.ptr = decode_register(modrm_reg, _regs, 0);
			switch ((dst.bytes = op_bytes)) {
			case 2:
				dst.val = *(u16 *)dst.ptr;
				break;
			case 4:
				dst.val = *(u32 *)dst.ptr;
				break;
			case 8:
				dst.val = *(u64 *)dst.ptr;
				break;
			}
		}
		break;
	case DstMem:
		dst.type = OP_MEM;
		dst.ptr = (unsigned long *)cr2;
		dst.bytes = (d & ByteOp) ? 1 : op_bytes;
		dst.val = 0;
		/*
		 * For instructions with a ModR/M byte, switch to register
		 * access if Mod = 3.
		 */
		if ((d & ModRM) && modrm_mod == 3) {
			dst.type = OP_REG;
			break;
		}
		if (d & BitOp) {
			unsigned long mask = ~(dst.bytes * 8 - 1);

			dst.ptr = (void *)dst.ptr + (src.val & mask) / 8;
		}
		if (!(d & Mov) && /* optimisation - avoid slow emulated read */
		    ((rc = ops->read_emulated((unsigned long)dst.ptr,
					      &dst.val, dst.bytes, ctxt->vcpu)) != 0))
			goto done;
		break;
	}
	dst.orig_val = dst.val;

	if (twobyte)
		goto twobyte_insn;

	switch (b) {
	case 0x00 ... 0x05:
	      add:		/* add */
		emulate_2op_SrcV("add", src, dst, _eflags);
		break;
	case 0x08 ... 0x0d:
	      or:		/* or */
		emulate_2op_SrcV("or", src, dst, _eflags);
		break;
	case 0x10 ... 0x15:
	      adc:		/* adc */
		emulate_2op_SrcV("adc", src, dst, _eflags);
		break;
	case 0x18 ... 0x1d:
	      sbb:		/* sbb */
		emulate_2op_SrcV("sbb", src, dst, _eflags);
		break;
	case 0x20 ... 0x23:
	      and:		/* and */
		emulate_2op_SrcV("and", src, dst, _eflags);
		break;
	case 0x24:              /* and al imm8 */
		dst.type = OP_REG;
		dst.ptr = &_regs[VCPU_REGS_RAX];
		dst.val = *(u8 *)dst.ptr;
		dst.bytes = 1;
		dst.orig_val = dst.val;
		goto and;
	case 0x25:              /* and ax imm16, or eax imm32 */
		dst.type = OP_REG;
		dst.bytes = op_bytes;
		dst.ptr = &_regs[VCPU_REGS_RAX];
		if (op_bytes == 2)
			dst.val = *(u16 *)dst.ptr;
		else
			dst.val = *(u32 *)dst.ptr;
		dst.orig_val = dst.val;
		goto and;
	case 0x28 ... 0x2d:
	      sub:		/* sub */
		emulate_2op_SrcV("sub", src, dst, _eflags);
		break;
	case 0x30 ... 0x35:
	      xor:		/* xor */
		emulate_2op_SrcV("xor", src, dst, _eflags);
		break;
	case 0x38 ... 0x3d:
	      cmp:		/* cmp */
		emulate_2op_SrcV("cmp", src, dst, _eflags);
		break;
	case 0x63:		/* movsxd */
		if (mode != X86EMUL_MODE_PROT64)
			goto cannot_emulate;
		dst.val = (s32) src.val;
		break;
	case 0x6a: /* push imm8 */
		src.val = 0L;
		src.val = insn_fetch(s8, 1, _eip);
push:
		dst.type  = OP_MEM;
		dst.bytes = op_bytes;
		dst.val = src.val;
		register_address_increment(_regs[VCPU_REGS_RSP], -op_bytes);
		dst.ptr = (void *) register_address(ctxt->ss_base,
							_regs[VCPU_REGS_RSP]);
		break;
	case 0x80 ... 0x83:	/* Grp1 */
		switch (modrm_reg) {
		case 0:
			goto add;
		case 1:
			goto or;
		case 2:
			goto adc;
		case 3:
			goto sbb;
		case 4:
			goto and;
		case 5:
			goto sub;
		case 6:
			goto xor;
		case 7:
			goto cmp;
		}
		break;
	case 0x84 ... 0x85:
	      test:		/* test */
		emulate_2op_SrcV("test", src, dst, _eflags);
		break;
	case 0x86 ... 0x87:	/* xchg */
		/* Write back the register source. */
		switch (dst.bytes) {
		case 1:
			*(u8 *) src.ptr = (u8) dst.val;
			break;
		case 2:
			*(u16 *) src.ptr = (u16) dst.val;
			break;
		case 4:
			*src.ptr = (u32) dst.val;
			break;	/* 64b reg: zero-extend */
		case 8:
			*src.ptr = dst.val;
			break;
		}
		/*
		 * Write back the memory destination with implicit LOCK
		 * prefix.
		 */
		dst.val = src.val;
		lock_prefix = 1;
		break;
	case 0x88 ... 0x8b:	/* mov */
		goto mov;
	case 0x8d: /* lea r16/r32, m */
		dst.val = modrm_val;
		break;
	case 0x8f:		/* pop (sole member of Grp1a) */
		/* 64-bit mode: POP always pops a 64-bit operand. */
		if (mode == X86EMUL_MODE_PROT64)
			dst.bytes = 8;
		if ((rc = ops->read_std(register_address(ctxt->ss_base,
							 _regs[VCPU_REGS_RSP]),
					&dst.val, dst.bytes, ctxt->vcpu)) != 0)
			goto done;
		register_address_increment(_regs[VCPU_REGS_RSP], dst.bytes);
		break;
	case 0xa0 ... 0xa1:	/* mov */
		dst.ptr = (unsigned long *)&_regs[VCPU_REGS_RAX];
		dst.val = src.val;
		_eip += ad_bytes;	/* skip src displacement */
		break;
	case 0xa2 ... 0xa3:	/* mov */
		dst.val = (unsigned long)_regs[VCPU_REGS_RAX];
		_eip += ad_bytes;	/* skip dst displacement */
		break;
	case 0xc0 ... 0xc1:
	      grp2:		/* Grp2 */
		switch (modrm_reg) {
		case 0:	/* rol */
			emulate_2op_SrcB("rol", src, dst, _eflags);
			break;
		case 1:	/* ror */
			emulate_2op_SrcB("ror", src, dst, _eflags);
			break;
		case 2:	/* rcl */
			emulate_2op_SrcB("rcl", src, dst, _eflags);
			break;
		case 3:	/* rcr */
			emulate_2op_SrcB("rcr", src, dst, _eflags);
			break;
		case 4:	/* sal/shl */
		case 6:	/* sal/shl */
			emulate_2op_SrcB("sal", src, dst, _eflags);
			break;
		case 5:	/* shr */
			emulate_2op_SrcB("shr", src, dst, _eflags);
			break;
		case 7:	/* sar */
			emulate_2op_SrcB("sar", src, dst, _eflags);
			break;
		}
		break;
	case 0xc6 ... 0xc7:	/* mov (sole member of Grp11) */
	mov:
		dst.val = src.val;
		break;
	case 0xd0 ... 0xd1:	/* Grp2 */
		src.val = 1;
		goto grp2;
	case 0xd2 ... 0xd3:	/* Grp2 */
		src.val = _regs[VCPU_REGS_RCX];
		goto grp2;
	case 0xf6 ... 0xf7:	/* Grp3 */
		switch (modrm_reg) {
		case 0 ... 1:	/* test */
			/*
			 * Special case in Grp3: test has an immediate
			 * source operand.
			 */
			src.type = OP_IMM;
			src.ptr = (unsigned long *)_eip;
			src.bytes = (d & ByteOp) ? 1 : op_bytes;
			if (src.bytes == 8)
				src.bytes = 4;
			switch (src.bytes) {
			case 1:
				src.val = insn_fetch(s8, 1, _eip);
				break;
			case 2:
				src.val = insn_fetch(s16, 2, _eip);
				break;
			case 4:
				src.val = insn_fetch(s32, 4, _eip);
				break;
			}
			goto test;
		case 2:	/* not */
			dst.val = ~dst.val;
			break;
		case 3:	/* neg */
			emulate_1op("neg", dst, _eflags);
			break;
		default:
			goto cannot_emulate;
		}
		break;
	case 0xfe ... 0xff:	/* Grp4/Grp5 */
		switch (modrm_reg) {
		case 0:	/* inc */
			emulate_1op("inc", dst, _eflags);
			break;
		case 1:	/* dec */
			emulate_1op("dec", dst, _eflags);
			break;
		case 4: /* jmp abs */
			if (b == 0xff)
				_eip = dst.val;
			else
				goto cannot_emulate;
			break;
		case 6:	/* push */
			/* 64-bit mode: PUSH always pushes a 64-bit operand. */
			if (mode == X86EMUL_MODE_PROT64) {
				dst.bytes = 8;
				if ((rc = ops->read_std((unsigned long)dst.ptr,
							&dst.val, 8,
							ctxt->vcpu)) != 0)
					goto done;
			}
			register_address_increment(_regs[VCPU_REGS_RSP],
						   -dst.bytes);
			if ((rc = ops->write_std(
				     register_address(ctxt->ss_base,
						      _regs[VCPU_REGS_RSP]),
				     &dst.val, dst.bytes, ctxt->vcpu)) != 0)
				goto done;
			no_wb = 1;
			break;
		default:
			goto cannot_emulate;
		}
		break;
	}

writeback:
	if (!no_wb) {
		switch (dst.type) {
		case OP_REG:
			/* The 4-byte case *is* correct: in 64-bit mode we zero-extend. */
			switch (dst.bytes) {
			case 1:
				*(u8 *)dst.ptr = (u8)dst.val;
				break;
			case 2:
				*(u16 *)dst.ptr = (u16)dst.val;
				break;
			case 4:
				*dst.ptr = (u32)dst.val;
				break;	/* 64b: zero-ext */
			case 8:
				*dst.ptr = dst.val;
				break;
			}
			break;
		case OP_MEM:
			if (lock_prefix)
				rc = ops->cmpxchg_emulated((unsigned long)dst.
							   ptr, &dst.orig_val,
							   &dst.val, dst.bytes,
							   ctxt->vcpu);
			else
				rc = ops->write_emulated((unsigned long)dst.ptr,
							 &dst.val, dst.bytes,
							 ctxt->vcpu);
			if (rc != 0)
				goto done;
		default:
			break;
		}
	}

	/* Commit shadow register state. */
	memcpy(ctxt->vcpu->regs, _regs, sizeof _regs);
	ctxt->eflags = _eflags;
	ctxt->vcpu->rip = _eip;

done:
	return (rc == X86EMUL_UNHANDLEABLE) ? -1 : 0;

special_insn:
	if (twobyte)
		goto twobyte_special_insn;
	switch(b) {
	case 0x50 ... 0x57:  /* push reg */
		if (op_bytes == 2)
			src.val = (u16) _regs[b & 0x7];
		else
			src.val = (u32) _regs[b & 0x7];
		dst.type  = OP_MEM;
		dst.bytes = op_bytes;
		dst.val = src.val;
		register_address_increment(_regs[VCPU_REGS_RSP], -op_bytes);
		dst.ptr = (void *) register_address(
			ctxt->ss_base, _regs[VCPU_REGS_RSP]);
		break;
	case 0x58 ... 0x5f: /* pop reg */
		dst.ptr = (unsigned long *)&_regs[b & 0x7];
	pop_instruction:
		if ((rc = ops->read_std(register_address(ctxt->ss_base,
			_regs[VCPU_REGS_RSP]), dst.ptr, op_bytes, ctxt->vcpu))
			!= 0)
			goto done;

		register_address_increment(_regs[VCPU_REGS_RSP], op_bytes);
		no_wb = 1; /* Disable writeback. */
		break;
	case 0x6c:		/* insb */
	case 0x6d:		/* insw/insd */
		 if (kvm_emulate_pio_string(ctxt->vcpu, NULL,
				1, 					/* in */
				(d & ByteOp) ? 1 : op_bytes, 		/* size */
				rep_prefix ?
				address_mask(_regs[VCPU_REGS_RCX]) : 1,	/* count */
				(_eflags & EFLG_DF),			/* down */
				register_address(ctxt->es_base,
						 _regs[VCPU_REGS_RDI]),	/* address */
				rep_prefix,
				_regs[VCPU_REGS_RDX]			/* port */
				) == 0)
			return -1;
		return 0;
	case 0x6e:		/* outsb */
	case 0x6f:		/* outsw/outsd */
		if (kvm_emulate_pio_string(ctxt->vcpu, NULL,
				0, 					/* in */
				(d & ByteOp) ? 1 : op_bytes, 		/* size */
				rep_prefix ?
				address_mask(_regs[VCPU_REGS_RCX]) : 1,	/* count */
				(_eflags & EFLG_DF),			/* down */
				register_address(override_base ?
						 *override_base : ctxt->ds_base,
						 _regs[VCPU_REGS_RSI]),	/* address */
				rep_prefix,
				_regs[VCPU_REGS_RDX]			/* port */
				) == 0)
			return -1;
		return 0;
	case 0x70 ... 0x7f: /* jcc (short) */ {
		int rel = insn_fetch(s8, 1, _eip);

		if (test_cc(b, _eflags))
		JMP_REL(rel);
		break;
	}
	case 0x9c: /* pushf */
		src.val =  (unsigned long) _eflags;
		goto push;
	case 0x9d: /* popf */
		dst.ptr = (unsigned long *) &_eflags;
		goto pop_instruction;
	case 0xc3: /* ret */
		dst.ptr = &_eip;
		goto pop_instruction;
	case 0xf4:              /* hlt */
		ctxt->vcpu->halt_request = 1;
		goto done;
	}
	if (rep_prefix) {
		if (_regs[VCPU_REGS_RCX] == 0) {
			ctxt->vcpu->rip = _eip;
			goto done;
		}
		_regs[VCPU_REGS_RCX]--;
		_eip = ctxt->vcpu->rip;
	}
	switch (b) {
	case 0xa4 ... 0xa5:	/* movs */
		dst.type = OP_MEM;
		dst.bytes = (d & ByteOp) ? 1 : op_bytes;
		dst.ptr = (unsigned long *)register_address(ctxt->es_base,
							_regs[VCPU_REGS_RDI]);
		if ((rc = ops->read_emulated(register_address(
		      override_base ? *override_base : ctxt->ds_base,
		      _regs[VCPU_REGS_RSI]), &dst.val, dst.bytes, ctxt->vcpu)) != 0)
			goto done;
		register_address_increment(_regs[VCPU_REGS_RSI],
			     (_eflags & EFLG_DF) ? -dst.bytes : dst.bytes);
		register_address_increment(_regs[VCPU_REGS_RDI],
			     (_eflags & EFLG_DF) ? -dst.bytes : dst.bytes);
		break;
	case 0xa6 ... 0xa7:	/* cmps */
		DPRINTF("Urk! I don't handle CMPS.\n");
		goto cannot_emulate;
	case 0xaa ... 0xab:	/* stos */
		dst.type = OP_MEM;
		dst.bytes = (d & ByteOp) ? 1 : op_bytes;
		dst.ptr = (unsigned long *)cr2;
		dst.val = _regs[VCPU_REGS_RAX];
		register_address_increment(_regs[VCPU_REGS_RDI],
			     (_eflags & EFLG_DF) ? -dst.bytes : dst.bytes);
		break;
	case 0xac ... 0xad:	/* lods */
		dst.type = OP_REG;
		dst.bytes = (d & ByteOp) ? 1 : op_bytes;
		dst.ptr = (unsigned long *)&_regs[VCPU_REGS_RAX];
		if ((rc = ops->read_emulated(cr2, &dst.val, dst.bytes,
					     ctxt->vcpu)) != 0)
			goto done;
		register_address_increment(_regs[VCPU_REGS_RSI],
			   (_eflags & EFLG_DF) ? -dst.bytes : dst.bytes);
		break;
	case 0xae ... 0xaf:	/* scas */
		DPRINTF("Urk! I don't handle SCAS.\n");
		goto cannot_emulate;
	case 0xe8: /* call (near) */ {
		long int rel;
		switch (op_bytes) {
		case 2:
			rel = insn_fetch(s16, 2, _eip);
			break;
		case 4:
			rel = insn_fetch(s32, 4, _eip);
			break;
		case 8:
			rel = insn_fetch(s64, 8, _eip);
			break;
		default:
			DPRINTF("Call: Invalid op_bytes\n");
			goto cannot_emulate;
		}
		src.val = (unsigned long) _eip;
		JMP_REL(rel);
		goto push;
	}
	case 0xe9: /* jmp rel */
	case 0xeb: /* jmp rel short */
		JMP_REL(src.val);
		no_wb = 1; /* Disable writeback. */
		break;


	}
	goto writeback;

twobyte_insn:
	switch (b) {
	case 0x01: /* lgdt, lidt, lmsw */
		/* Disable writeback. */
		no_wb = 1;
		switch (modrm_reg) {
			u16 size;
			unsigned long address;

		case 2: /* lgdt */
			rc = read_descriptor(ctxt, ops, src.ptr,
					     &size, &address, op_bytes);
			if (rc)
				goto done;
			realmode_lgdt(ctxt->vcpu, size, address);
			break;
		case 3: /* lidt */
			rc = read_descriptor(ctxt, ops, src.ptr,
					     &size, &address, op_bytes);
			if (rc)
				goto done;
			realmode_lidt(ctxt->vcpu, size, address);
			break;
		case 4: /* smsw */
			if (modrm_mod != 3)
				goto cannot_emulate;
			*(u16 *)&_regs[modrm_rm]
				= realmode_get_cr(ctxt->vcpu, 0);
			break;
		case 6: /* lmsw */
			if (modrm_mod != 3)
				goto cannot_emulate;
			realmode_lmsw(ctxt->vcpu, (u16)modrm_val, &_eflags);
			break;
		case 7: /* invlpg*/
			emulate_invlpg(ctxt->vcpu, cr2);
			break;
		default:
			goto cannot_emulate;
		}
		break;
	case 0x21: /* mov from dr to reg */
		no_wb = 1;
		if (modrm_mod != 3)
			goto cannot_emulate;
		rc = emulator_get_dr(ctxt, modrm_reg, &_regs[modrm_rm]);
		break;
	case 0x23: /* mov from reg to dr */
		no_wb = 1;
		if (modrm_mod != 3)
			goto cannot_emulate;
		rc = emulator_set_dr(ctxt, modrm_reg, _regs[modrm_rm]);
		break;
	case 0x40 ... 0x4f:	/* cmov */
		dst.val = dst.orig_val = src.val;
		no_wb = 1;
		/*
		 * First, assume we're decoding an even cmov opcode
		 * (lsb == 0).
		 */
		switch ((b & 15) >> 1) {
		case 0:	/* cmovo */
			no_wb = (_eflags & EFLG_OF) ? 0 : 1;
			break;
		case 1:	/* cmovb/cmovc/cmovnae */
			no_wb = (_eflags & EFLG_CF) ? 0 : 1;
			break;
		case 2:	/* cmovz/cmove */
			no_wb = (_eflags & EFLG_ZF) ? 0 : 1;
			break;
		case 3:	/* cmovbe/cmovna */
			no_wb = (_eflags & (EFLG_CF | EFLG_ZF)) ? 0 : 1;
			break;
		case 4:	/* cmovs */
			no_wb = (_eflags & EFLG_SF) ? 0 : 1;
			break;
		case 5:	/* cmovp/cmovpe */
			no_wb = (_eflags & EFLG_PF) ? 0 : 1;
			break;
		case 7:	/* cmovle/cmovng */
			no_wb = (_eflags & EFLG_ZF) ? 0 : 1;
			/* fall through */
		case 6:	/* cmovl/cmovnge */
			no_wb &= (!(_eflags & EFLG_SF) !=
			      !(_eflags & EFLG_OF)) ? 0 : 1;
			break;
		}
		/* Odd cmov opcodes (lsb == 1) have inverted sense. */
		no_wb ^= b & 1;
		break;
	case 0xa3:
	      bt:		/* bt */
		src.val &= (dst.bytes << 3) - 1; /* only subword offset */
		emulate_2op_SrcV_nobyte("bt", src, dst, _eflags);
		break;
	case 0xab:
	      bts:		/* bts */
		src.val &= (dst.bytes << 3) - 1; /* only subword offset */
		emulate_2op_SrcV_nobyte("bts", src, dst, _eflags);
		break;
	case 0xb0 ... 0xb1:	/* cmpxchg */
		/*
		 * Save real source value, then compare EAX against
		 * destination.
		 */
		src.orig_val = src.val;
		src.val = _regs[VCPU_REGS_RAX];
		emulate_2op_SrcV("cmp", src, dst, _eflags);
		if (_eflags & EFLG_ZF) {
			/* Success: write back to memory. */
			dst.val = src.orig_val;
		} else {
			/* Failure: write the value we saw to EAX. */
			dst.type = OP_REG;
			dst.ptr = (unsigned long *)&_regs[VCPU_REGS_RAX];
		}
		break;
	case 0xb3:
	      btr:		/* btr */
		src.val &= (dst.bytes << 3) - 1; /* only subword offset */
		emulate_2op_SrcV_nobyte("btr", src, dst, _eflags);
		break;
	case 0xb6 ... 0xb7:	/* movzx */
		dst.bytes = op_bytes;
		dst.val = (d & ByteOp) ? (u8) src.val : (u16) src.val;
		break;
	case 0xba:		/* Grp8 */
		switch (modrm_reg & 3) {
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
		src.val &= (dst.bytes << 3) - 1; /* only subword offset */
		emulate_2op_SrcV_nobyte("btc", src, dst, _eflags);
		break;
	case 0xbe ... 0xbf:	/* movsx */
		dst.bytes = op_bytes;
		dst.val = (d & ByteOp) ? (s8) src.val : (s16) src.val;
		break;
	case 0xc3:		/* movnti */
		dst.bytes = op_bytes;
		dst.val = (op_bytes == 4) ? (u32) src.val : (u64) src.val;
		break;
	}
	goto writeback;

twobyte_special_insn:
	/* Disable writeback. */
	no_wb = 1;
	switch (b) {
	case 0x06:
		emulate_clts(ctxt->vcpu);
		break;
	case 0x09:		/* wbinvd */
		break;
	case 0x0d:		/* GrpP (prefetch) */
	case 0x18:		/* Grp16 (prefetch/nop) */
		break;
	case 0x20: /* mov cr, reg */
		if (modrm_mod != 3)
			goto cannot_emulate;
		_regs[modrm_rm] = realmode_get_cr(ctxt->vcpu, modrm_reg);
		break;
	case 0x22: /* mov reg, cr */
		if (modrm_mod != 3)
			goto cannot_emulate;
		realmode_set_cr(ctxt->vcpu, modrm_reg, modrm_val, &_eflags);
		break;
	case 0x30:
		/* wrmsr */
		msr_data = (u32)_regs[VCPU_REGS_RAX]
			| ((u64)_regs[VCPU_REGS_RDX] << 32);
		rc = kvm_set_msr(ctxt->vcpu, _regs[VCPU_REGS_RCX], msr_data);
		if (rc) {
			kvm_x86_ops->inject_gp(ctxt->vcpu, 0);
			_eip = ctxt->vcpu->rip;
		}
		rc = X86EMUL_CONTINUE;
		break;
	case 0x32:
		/* rdmsr */
		rc = kvm_get_msr(ctxt->vcpu, _regs[VCPU_REGS_RCX], &msr_data);
		if (rc) {
			kvm_x86_ops->inject_gp(ctxt->vcpu, 0);
			_eip = ctxt->vcpu->rip;
		} else {
			_regs[VCPU_REGS_RAX] = (u32)msr_data;
			_regs[VCPU_REGS_RDX] = msr_data >> 32;
		}
		rc = X86EMUL_CONTINUE;
		break;
	case 0x80 ... 0x8f: /* jnz rel, etc*/ {
		long int rel;

		switch (op_bytes) {
		case 2:
			rel = insn_fetch(s16, 2, _eip);
			break;
		case 4:
			rel = insn_fetch(s32, 4, _eip);
			break;
		case 8:
			rel = insn_fetch(s64, 8, _eip);
			break;
		default:
			DPRINTF("jnz: Invalid op_bytes\n");
			goto cannot_emulate;
		}
		if (test_cc(b, _eflags))
			JMP_REL(rel);
		break;
	}
	case 0xc7:		/* Grp9 (cmpxchg8b) */
		{
			u64 old, new;
			if ((rc = ops->read_emulated(cr2, &old, 8, ctxt->vcpu))
									!= 0)
				goto done;
			if (((u32) (old >> 0) != (u32) _regs[VCPU_REGS_RAX]) ||
			    ((u32) (old >> 32) != (u32) _regs[VCPU_REGS_RDX])) {
				_regs[VCPU_REGS_RAX] = (u32) (old >> 0);
				_regs[VCPU_REGS_RDX] = (u32) (old >> 32);
				_eflags &= ~EFLG_ZF;
			} else {
				new = ((u64)_regs[VCPU_REGS_RCX] << 32)
					| (u32) _regs[VCPU_REGS_RBX];
				if ((rc = ops->cmpxchg_emulated(cr2, &old,
							  &new, 8, ctxt->vcpu)) != 0)
					goto done;
				_eflags |= EFLG_ZF;
			}
			break;
		}
	}
	goto writeback;

cannot_emulate:
	DPRINTF("Cannot emulate %02x\n", b);
	return -1;
}

#ifdef __XEN__

#include <asm/mm.h>
#include <asm/uaccess.h>

int
x86_emulate_read_std(unsigned long addr,
		     unsigned long *val,
		     unsigned int bytes, struct x86_emulate_ctxt *ctxt)
{
	unsigned int rc;

	*val = 0;

	if ((rc = copy_from_user((void *)val, (void *)addr, bytes)) != 0) {
		propagate_page_fault(addr + bytes - rc, 0);	/* read fault */
		return X86EMUL_PROPAGATE_FAULT;
	}

	return X86EMUL_CONTINUE;
}

int
x86_emulate_write_std(unsigned long addr,
		      unsigned long val,
		      unsigned int bytes, struct x86_emulate_ctxt *ctxt)
{
	unsigned int rc;

	if ((rc = copy_to_user((void *)addr, (void *)&val, bytes)) != 0) {
		propagate_page_fault(addr + bytes - rc, PGERR_write_access);
		return X86EMUL_PROPAGATE_FAULT;
	}

	return X86EMUL_CONTINUE;
}

#endif
