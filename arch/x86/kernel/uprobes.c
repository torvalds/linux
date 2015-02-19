/*
 * User-space Probes (UProbes) for x86
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2008-2011
 * Authors:
 *	Srikar Dronamraju
 *	Jim Keniston
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/uprobes.h>
#include <linux/uaccess.h>

#include <linux/kdebug.h>
#include <asm/processor.h>
#include <asm/insn.h>

/* Post-execution fixups. */

/* Adjust IP back to vicinity of actual insn */
#define UPROBE_FIX_IP		0x01

/* Adjust the return address of a call insn */
#define UPROBE_FIX_CALL		0x02

/* Instruction will modify TF, don't change it */
#define UPROBE_FIX_SETF		0x04

#define UPROBE_FIX_RIP_SI	0x08
#define UPROBE_FIX_RIP_DI	0x10
#define UPROBE_FIX_RIP_BX	0x20
#define UPROBE_FIX_RIP_MASK	\
	(UPROBE_FIX_RIP_SI | UPROBE_FIX_RIP_DI | UPROBE_FIX_RIP_BX)

#define	UPROBE_TRAP_NR		UINT_MAX

/* Adaptations for mhiramat x86 decoder v14. */
#define OPCODE1(insn)		((insn)->opcode.bytes[0])
#define OPCODE2(insn)		((insn)->opcode.bytes[1])
#define OPCODE3(insn)		((insn)->opcode.bytes[2])
#define MODRM_REG(insn)		X86_MODRM_REG((insn)->modrm.value)

#define W(row, b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, ba, bb, bc, bd, be, bf)\
	(((b0##UL << 0x0)|(b1##UL << 0x1)|(b2##UL << 0x2)|(b3##UL << 0x3) |   \
	  (b4##UL << 0x4)|(b5##UL << 0x5)|(b6##UL << 0x6)|(b7##UL << 0x7) |   \
	  (b8##UL << 0x8)|(b9##UL << 0x9)|(ba##UL << 0xa)|(bb##UL << 0xb) |   \
	  (bc##UL << 0xc)|(bd##UL << 0xd)|(be##UL << 0xe)|(bf##UL << 0xf))    \
	 << (row % 32))

/*
 * Good-instruction tables for 32-bit apps.  This is non-const and volatile
 * to keep gcc from statically optimizing it out, as variable_test_bit makes
 * some versions of gcc to think only *(unsigned long*) is used.
 */
#if defined(CONFIG_X86_32) || defined(CONFIG_IA32_EMULATION)
static volatile u32 good_insns_32[256 / 32] = {
	/*      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f         */
	/*      ----------------------------------------------         */
	W(0x00, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0) | /* 00 */
	W(0x10, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0) , /* 10 */
	W(0x20, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1) | /* 20 */
	W(0x30, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1) , /* 30 */
	W(0x40, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) | /* 40 */
	W(0x50, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* 50 */
	W(0x60, 1, 1, 1, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0) | /* 60 */
	W(0x70, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* 70 */
	W(0x80, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) | /* 80 */
	W(0x90, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* 90 */
	W(0xa0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) | /* a0 */
	W(0xb0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* b0 */
	W(0xc0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0) | /* c0 */
	W(0xd0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* d0 */
	W(0xe0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0) | /* e0 */
	W(0xf0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1)   /* f0 */
	/*      ----------------------------------------------         */
	/*      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f         */
};
#else
#define good_insns_32	NULL
#endif

/* Good-instruction tables for 64-bit apps */
#if defined(CONFIG_X86_64)
static volatile u32 good_insns_64[256 / 32] = {
	/*      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f         */
	/*      ----------------------------------------------         */
	W(0x00, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0) | /* 00 */
	W(0x10, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0) , /* 10 */
	W(0x20, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0) | /* 20 */
	W(0x30, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0) , /* 30 */
	W(0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) | /* 40 */
	W(0x50, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* 50 */
	W(0x60, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0) | /* 60 */
	W(0x70, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* 70 */
	W(0x80, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) | /* 80 */
	W(0x90, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* 90 */
	W(0xa0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) | /* a0 */
	W(0xb0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* b0 */
	W(0xc0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0) | /* c0 */
	W(0xd0, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* d0 */
	W(0xe0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0) | /* e0 */
	W(0xf0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1)   /* f0 */
	/*      ----------------------------------------------         */
	/*      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f         */
};
#else
#define good_insns_64	NULL
#endif

/* Using this for both 64-bit and 32-bit apps */
static volatile u32 good_2byte_insns[256 / 32] = {
	/*      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f         */
	/*      ----------------------------------------------         */
	W(0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1) | /* 00 */
	W(0x10, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1) , /* 10 */
	W(0x20, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1) | /* 20 */
	W(0x30, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) , /* 30 */
	W(0x40, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) | /* 40 */
	W(0x50, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* 50 */
	W(0x60, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) | /* 60 */
	W(0x70, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1) , /* 70 */
	W(0x80, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) | /* 80 */
	W(0x90, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* 90 */
	W(0xa0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1) | /* a0 */
	W(0xb0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1) , /* b0 */
	W(0xc0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) | /* c0 */
	W(0xd0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* d0 */
	W(0xe0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) | /* e0 */
	W(0xf0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0)   /* f0 */
	/*      ----------------------------------------------         */
	/*      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f         */
};
#undef W

/*
 * opcodes we'll probably never support:
 *
 *  6c-6d, e4-e5, ec-ed - in
 *  6e-6f, e6-e7, ee-ef - out
 *  cc, cd - int3, int
 *  cf - iret
 *  d6 - illegal instruction
 *  f1 - int1/icebp
 *  f4 - hlt
 *  fa, fb - cli, sti
 *  0f - lar, lsl, syscall, clts, sysret, sysenter, sysexit, invd, wbinvd, ud2
 *
 * invalid opcodes in 64-bit mode:
 *
 *  06, 0e, 16, 1e, 27, 2f, 37, 3f, 60-62, 82, c4-c5, d4-d5
 *  63 - we support this opcode in x86_64 but not in i386.
 *
 * opcodes we may need to refine support for:
 *
 *  0f - 2-byte instructions: For many of these instructions, the validity
 *  depends on the prefix and/or the reg field.  On such instructions, we
 *  just consider the opcode combination valid if it corresponds to any
 *  valid instruction.
 *
 *  8f - Group 1 - only reg = 0 is OK
 *  c6-c7 - Group 11 - only reg = 0 is OK
 *  d9-df - fpu insns with some illegal encodings
 *  f2, f3 - repnz, repz prefixes.  These are also the first byte for
 *  certain floating-point instructions, such as addsd.
 *
 *  fe - Group 4 - only reg = 0 or 1 is OK
 *  ff - Group 5 - only reg = 0-6 is OK
 *
 * others -- Do we need to support these?
 *
 *  0f - (floating-point?) prefetch instructions
 *  07, 17, 1f - pop es, pop ss, pop ds
 *  26, 2e, 36, 3e - es:, cs:, ss:, ds: segment prefixes --
 *	but 64 and 65 (fs: and gs:) seem to be used, so we support them
 *  67 - addr16 prefix
 *  ce - into
 *  f0 - lock prefix
 */

/*
 * TODO:
 * - Where necessary, examine the modrm byte and allow only valid instructions
 * in the different Groups and fpu instructions.
 */

static bool is_prefix_bad(struct insn *insn)
{
	int i;

	for (i = 0; i < insn->prefixes.nbytes; i++) {
		switch (insn->prefixes.bytes[i]) {
		case 0x26:	/* INAT_PFX_ES   */
		case 0x2E:	/* INAT_PFX_CS   */
		case 0x36:	/* INAT_PFX_DS   */
		case 0x3E:	/* INAT_PFX_SS   */
		case 0xF0:	/* INAT_PFX_LOCK */
			return true;
		}
	}
	return false;
}

static int uprobe_init_insn(struct arch_uprobe *auprobe, struct insn *insn, bool x86_64)
{
	u32 volatile *good_insns;

	insn_init(insn, auprobe->insn, sizeof(auprobe->insn), x86_64);
	/* has the side-effect of processing the entire instruction */
	insn_get_length(insn);
	if (WARN_ON_ONCE(!insn_complete(insn)))
		return -ENOEXEC;

	if (is_prefix_bad(insn))
		return -ENOTSUPP;

	if (x86_64)
		good_insns = good_insns_64;
	else
		good_insns = good_insns_32;

	if (test_bit(OPCODE1(insn), (unsigned long *)good_insns))
		return 0;

	if (insn->opcode.nbytes == 2) {
		if (test_bit(OPCODE2(insn), (unsigned long *)good_2byte_insns))
			return 0;
	}

	return -ENOTSUPP;
}

#ifdef CONFIG_X86_64
static inline bool is_64bit_mm(struct mm_struct *mm)
{
	return	!config_enabled(CONFIG_IA32_EMULATION) ||
		!(mm->context.ia32_compat == TIF_IA32);
}
/*
 * If arch_uprobe->insn doesn't use rip-relative addressing, return
 * immediately.  Otherwise, rewrite the instruction so that it accesses
 * its memory operand indirectly through a scratch register.  Set
 * defparam->fixups accordingly. (The contents of the scratch register
 * will be saved before we single-step the modified instruction,
 * and restored afterward).
 *
 * We do this because a rip-relative instruction can access only a
 * relatively small area (+/- 2 GB from the instruction), and the XOL
 * area typically lies beyond that area.  At least for instructions
 * that store to memory, we can't execute the original instruction
 * and "fix things up" later, because the misdirected store could be
 * disastrous.
 *
 * Some useful facts about rip-relative instructions:
 *
 *  - There's always a modrm byte with bit layout "00 reg 101".
 *  - There's never a SIB byte.
 *  - The displacement is always 4 bytes.
 *  - REX.B=1 bit in REX prefix, which normally extends r/m field,
 *    has no effect on rip-relative mode. It doesn't make modrm byte
 *    with r/m=101 refer to register 1101 = R13.
 */
static void riprel_analyze(struct arch_uprobe *auprobe, struct insn *insn)
{
	u8 *cursor;
	u8 reg;
	u8 reg2;

	if (!insn_rip_relative(insn))
		return;

	/*
	 * insn_rip_relative() would have decoded rex_prefix, vex_prefix, modrm.
	 * Clear REX.b bit (extension of MODRM.rm field):
	 * we want to encode low numbered reg, not r8+.
	 */
	if (insn->rex_prefix.nbytes) {
		cursor = auprobe->insn + insn_offset_rex_prefix(insn);
		/* REX byte has 0100wrxb layout, clearing REX.b bit */
		*cursor &= 0xfe;
	}
	/*
	 * Similar treatment for VEX3 prefix.
	 * TODO: add XOP/EVEX treatment when insn decoder supports them
	 */
	if (insn->vex_prefix.nbytes == 3) {
		/*
		 * vex2:     c5    rvvvvLpp   (has no b bit)
		 * vex3/xop: c4/8f rxbmmmmm wvvvvLpp
		 * evex:     62    rxbR00mm wvvvv1pp zllBVaaa
		 *   (evex will need setting of both b and x since
		 *   in non-sib encoding evex.x is 4th bit of MODRM.rm)
		 * Setting VEX3.b (setting because it has inverted meaning):
		 */
		cursor = auprobe->insn + insn_offset_vex_prefix(insn) + 1;
		*cursor |= 0x20;
	}

	/*
	 * Convert from rip-relative addressing to register-relative addressing
	 * via a scratch register.
	 *
	 * This is tricky since there are insns with modrm byte
	 * which also use registers not encoded in modrm byte:
	 * [i]div/[i]mul: implicitly use dx:ax
	 * shift ops: implicitly use cx
	 * cmpxchg: implicitly uses ax
	 * cmpxchg8/16b: implicitly uses dx:ax and bx:cx
	 *   Encoding: 0f c7/1 modrm
	 *   The code below thinks that reg=1 (cx), chooses si as scratch.
	 * mulx: implicitly uses dx: mulx r/m,r1,r2 does r1:r2 = dx * r/m.
	 *   First appeared in Haswell (BMI2 insn). It is vex-encoded.
	 *   Example where none of bx,cx,dx can be used as scratch reg:
	 *   c4 e2 63 f6 0d disp32   mulx disp32(%rip),%ebx,%ecx
	 * [v]pcmpistri: implicitly uses cx, xmm0
	 * [v]pcmpistrm: implicitly uses xmm0
	 * [v]pcmpestri: implicitly uses ax, dx, cx, xmm0
	 * [v]pcmpestrm: implicitly uses ax, dx, xmm0
	 *   Evil SSE4.2 string comparison ops from hell.
	 * maskmovq/[v]maskmovdqu: implicitly uses (ds:rdi) as destination.
	 *   Encoding: 0f f7 modrm, 66 0f f7 modrm, vex-encoded: c5 f9 f7 modrm.
	 *   Store op1, byte-masked by op2 msb's in each byte, to (ds:rdi).
	 *   AMD says it has no 3-operand form (vex.vvvv must be 1111)
	 *   and that it can have only register operands, not mem
	 *   (its modrm byte must have mode=11).
	 *   If these restrictions will ever be lifted,
	 *   we'll need code to prevent selection of di as scratch reg!
	 *
	 * Summary: I don't know any insns with modrm byte which
	 * use SI register implicitly. DI register is used only
	 * by one insn (maskmovq) and BX register is used
	 * only by one too (cmpxchg8b).
	 * BP is stack-segment based (may be a problem?).
	 * AX, DX, CX are off-limits (many implicit users).
	 * SP is unusable (it's stack pointer - think about "pop mem";
	 * also, rsp+disp32 needs sib encoding -> insn length change).
	 */

	reg = MODRM_REG(insn);	/* Fetch modrm.reg */
	reg2 = 0xff;		/* Fetch vex.vvvv */
	if (insn->vex_prefix.nbytes == 2)
		reg2 = insn->vex_prefix.bytes[1];
	else if (insn->vex_prefix.nbytes == 3)
		reg2 = insn->vex_prefix.bytes[2];
	/*
	 * TODO: add XOP, EXEV vvvv reading.
	 *
	 * vex.vvvv field is in bits 6-3, bits are inverted.
	 * But in 32-bit mode, high-order bit may be ignored.
	 * Therefore, let's consider only 3 low-order bits.
	 */
	reg2 = ((reg2 >> 3) & 0x7) ^ 0x7;
	/*
	 * Register numbering is ax,cx,dx,bx, sp,bp,si,di, r8..r15.
	 *
	 * Choose scratch reg. Order is important: must not select bx
	 * if we can use si (cmpxchg8b case!)
	 */
	if (reg != 6 && reg2 != 6) {
		reg2 = 6;
		auprobe->defparam.fixups |= UPROBE_FIX_RIP_SI;
	} else if (reg != 7 && reg2 != 7) {
		reg2 = 7;
		auprobe->defparam.fixups |= UPROBE_FIX_RIP_DI;
		/* TODO (paranoia): force maskmovq to not use di */
	} else {
		reg2 = 3;
		auprobe->defparam.fixups |= UPROBE_FIX_RIP_BX;
	}
	/*
	 * Point cursor at the modrm byte.  The next 4 bytes are the
	 * displacement.  Beyond the displacement, for some instructions,
	 * is the immediate operand.
	 */
	cursor = auprobe->insn + insn_offset_modrm(insn);
	/*
	 * Change modrm from "00 reg 101" to "10 reg reg2". Example:
	 * 89 05 disp32  mov %eax,disp32(%rip) becomes
	 * 89 86 disp32  mov %eax,disp32(%rsi)
	 */
	*cursor = 0x80 | (reg << 3) | reg2;
}

static inline unsigned long *
scratch_reg(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	if (auprobe->defparam.fixups & UPROBE_FIX_RIP_SI)
		return &regs->si;
	if (auprobe->defparam.fixups & UPROBE_FIX_RIP_DI)
		return &regs->di;
	return &regs->bx;
}

/*
 * If we're emulating a rip-relative instruction, save the contents
 * of the scratch register and store the target address in that register.
 */
static void riprel_pre_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	if (auprobe->defparam.fixups & UPROBE_FIX_RIP_MASK) {
		struct uprobe_task *utask = current->utask;
		unsigned long *sr = scratch_reg(auprobe, regs);

		utask->autask.saved_scratch_register = *sr;
		*sr = utask->vaddr + auprobe->defparam.ilen;
	}
}

static void riprel_post_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	if (auprobe->defparam.fixups & UPROBE_FIX_RIP_MASK) {
		struct uprobe_task *utask = current->utask;
		unsigned long *sr = scratch_reg(auprobe, regs);

		*sr = utask->autask.saved_scratch_register;
	}
}
#else /* 32-bit: */
static inline bool is_64bit_mm(struct mm_struct *mm)
{
	return false;
}
/*
 * No RIP-relative addressing on 32-bit
 */
static void riprel_analyze(struct arch_uprobe *auprobe, struct insn *insn)
{
}
static void riprel_pre_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
}
static void riprel_post_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
}
#endif /* CONFIG_X86_64 */

struct uprobe_xol_ops {
	bool	(*emulate)(struct arch_uprobe *, struct pt_regs *);
	int	(*pre_xol)(struct arch_uprobe *, struct pt_regs *);
	int	(*post_xol)(struct arch_uprobe *, struct pt_regs *);
	void	(*abort)(struct arch_uprobe *, struct pt_regs *);
};

static inline int sizeof_long(void)
{
	return is_ia32_task() ? 4 : 8;
}

static int default_pre_xol_op(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	riprel_pre_xol(auprobe, regs);
	return 0;
}

static int push_ret_address(struct pt_regs *regs, unsigned long ip)
{
	unsigned long new_sp = regs->sp - sizeof_long();

	if (copy_to_user((void __user *)new_sp, &ip, sizeof_long()))
		return -EFAULT;

	regs->sp = new_sp;
	return 0;
}

/*
 * We have to fix things up as follows:
 *
 * Typically, the new ip is relative to the copied instruction.  We need
 * to make it relative to the original instruction (FIX_IP).  Exceptions
 * are return instructions and absolute or indirect jump or call instructions.
 *
 * If the single-stepped instruction was a call, the return address that
 * is atop the stack is the address following the copied instruction.  We
 * need to make it the address following the original instruction (FIX_CALL).
 *
 * If the original instruction was a rip-relative instruction such as
 * "movl %edx,0xnnnn(%rip)", we have instead executed an equivalent
 * instruction using a scratch register -- e.g., "movl %edx,0xnnnn(%rsi)".
 * We need to restore the contents of the scratch register
 * (FIX_RIP_reg).
 */
static int default_post_xol_op(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	riprel_post_xol(auprobe, regs);
	if (auprobe->defparam.fixups & UPROBE_FIX_IP) {
		long correction = utask->vaddr - utask->xol_vaddr;
		regs->ip += correction;
	} else if (auprobe->defparam.fixups & UPROBE_FIX_CALL) {
		regs->sp += sizeof_long(); /* Pop incorrect return address */
		if (push_ret_address(regs, utask->vaddr + auprobe->defparam.ilen))
			return -ERESTART;
	}
	/* popf; tell the caller to not touch TF */
	if (auprobe->defparam.fixups & UPROBE_FIX_SETF)
		utask->autask.saved_tf = true;

	return 0;
}

static void default_abort_op(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	riprel_post_xol(auprobe, regs);
}

static struct uprobe_xol_ops default_xol_ops = {
	.pre_xol  = default_pre_xol_op,
	.post_xol = default_post_xol_op,
	.abort	  = default_abort_op,
};

static bool branch_is_call(struct arch_uprobe *auprobe)
{
	return auprobe->branch.opc1 == 0xe8;
}

#define CASE_COND					\
	COND(70, 71, XF(OF))				\
	COND(72, 73, XF(CF))				\
	COND(74, 75, XF(ZF))				\
	COND(78, 79, XF(SF))				\
	COND(7a, 7b, XF(PF))				\
	COND(76, 77, XF(CF) || XF(ZF))			\
	COND(7c, 7d, XF(SF) != XF(OF))			\
	COND(7e, 7f, XF(ZF) || XF(SF) != XF(OF))

#define COND(op_y, op_n, expr)				\
	case 0x ## op_y: DO((expr) != 0)		\
	case 0x ## op_n: DO((expr) == 0)

#define XF(xf)	(!!(flags & X86_EFLAGS_ ## xf))

static bool is_cond_jmp_opcode(u8 opcode)
{
	switch (opcode) {
	#define DO(expr)	\
		return true;
	CASE_COND
	#undef	DO

	default:
		return false;
	}
}

static bool check_jmp_cond(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	unsigned long flags = regs->flags;

	switch (auprobe->branch.opc1) {
	#define DO(expr)	\
		return expr;
	CASE_COND
	#undef	DO

	default:	/* not a conditional jmp */
		return true;
	}
}

#undef	XF
#undef	COND
#undef	CASE_COND

static bool branch_emulate_op(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	unsigned long new_ip = regs->ip += auprobe->branch.ilen;
	unsigned long offs = (long)auprobe->branch.offs;

	if (branch_is_call(auprobe)) {
		/*
		 * If it fails we execute this (mangled, see the comment in
		 * branch_clear_offset) insn out-of-line. In the likely case
		 * this should trigger the trap, and the probed application
		 * should die or restart the same insn after it handles the
		 * signal, arch_uprobe_post_xol() won't be even called.
		 *
		 * But there is corner case, see the comment in ->post_xol().
		 */
		if (push_ret_address(regs, new_ip))
			return false;
	} else if (!check_jmp_cond(auprobe, regs)) {
		offs = 0;
	}

	regs->ip = new_ip + offs;
	return true;
}

static int branch_post_xol_op(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	BUG_ON(!branch_is_call(auprobe));
	/*
	 * We can only get here if branch_emulate_op() failed to push the ret
	 * address _and_ another thread expanded our stack before the (mangled)
	 * "call" insn was executed out-of-line. Just restore ->sp and restart.
	 * We could also restore ->ip and try to call branch_emulate_op() again.
	 */
	regs->sp += sizeof_long();
	return -ERESTART;
}

static void branch_clear_offset(struct arch_uprobe *auprobe, struct insn *insn)
{
	/*
	 * Turn this insn into "call 1f; 1:", this is what we will execute
	 * out-of-line if ->emulate() fails. We only need this to generate
	 * a trap, so that the probed task receives the correct signal with
	 * the properly filled siginfo.
	 *
	 * But see the comment in ->post_xol(), in the unlikely case it can
	 * succeed. So we need to ensure that the new ->ip can not fall into
	 * the non-canonical area and trigger #GP.
	 *
	 * We could turn it into (say) "pushf", but then we would need to
	 * divorce ->insn[] and ->ixol[]. We need to preserve the 1st byte
	 * of ->insn[] for set_orig_insn().
	 */
	memset(auprobe->insn + insn_offset_immediate(insn),
		0, insn->immediate.nbytes);
}

static struct uprobe_xol_ops branch_xol_ops = {
	.emulate  = branch_emulate_op,
	.post_xol = branch_post_xol_op,
};

/* Returns -ENOSYS if branch_xol_ops doesn't handle this insn */
static int branch_setup_xol_ops(struct arch_uprobe *auprobe, struct insn *insn)
{
	u8 opc1 = OPCODE1(insn);
	int i;

	switch (opc1) {
	case 0xeb:	/* jmp 8 */
	case 0xe9:	/* jmp 32 */
	case 0x90:	/* prefix* + nop; same as jmp with .offs = 0 */
		break;

	case 0xe8:	/* call relative */
		branch_clear_offset(auprobe, insn);
		break;

	case 0x0f:
		if (insn->opcode.nbytes != 2)
			return -ENOSYS;
		/*
		 * If it is a "near" conditional jmp, OPCODE2() - 0x10 matches
		 * OPCODE1() of the "short" jmp which checks the same condition.
		 */
		opc1 = OPCODE2(insn) - 0x10;
	default:
		if (!is_cond_jmp_opcode(opc1))
			return -ENOSYS;
	}

	/*
	 * 16-bit overrides such as CALLW (66 e8 nn nn) are not supported.
	 * Intel and AMD behavior differ in 64-bit mode: Intel ignores 66 prefix.
	 * No one uses these insns, reject any branch insns with such prefix.
	 */
	for (i = 0; i < insn->prefixes.nbytes; i++) {
		if (insn->prefixes.bytes[i] == 0x66)
			return -ENOTSUPP;
	}

	auprobe->branch.opc1 = opc1;
	auprobe->branch.ilen = insn->length;
	auprobe->branch.offs = insn->immediate.value;

	auprobe->ops = &branch_xol_ops;
	return 0;
}

/**
 * arch_uprobe_analyze_insn - instruction analysis including validity and fixups.
 * @mm: the probed address space.
 * @arch_uprobe: the probepoint information.
 * @addr: virtual address at which to install the probepoint
 * Return 0 on success or a -ve number on error.
 */
int arch_uprobe_analyze_insn(struct arch_uprobe *auprobe, struct mm_struct *mm, unsigned long addr)
{
	struct insn insn;
	u8 fix_ip_or_call = UPROBE_FIX_IP;
	int ret;

	ret = uprobe_init_insn(auprobe, &insn, is_64bit_mm(mm));
	if (ret)
		return ret;

	ret = branch_setup_xol_ops(auprobe, &insn);
	if (ret != -ENOSYS)
		return ret;

	/*
	 * Figure out which fixups default_post_xol_op() will need to perform,
	 * and annotate defparam->fixups accordingly.
	 */
	switch (OPCODE1(&insn)) {
	case 0x9d:		/* popf */
		auprobe->defparam.fixups |= UPROBE_FIX_SETF;
		break;
	case 0xc3:		/* ret or lret -- ip is correct */
	case 0xcb:
	case 0xc2:
	case 0xca:
	case 0xea:		/* jmp absolute -- ip is correct */
		fix_ip_or_call = 0;
		break;
	case 0x9a:		/* call absolute - Fix return addr, not ip */
		fix_ip_or_call = UPROBE_FIX_CALL;
		break;
	case 0xff:
		switch (MODRM_REG(&insn)) {
		case 2: case 3:			/* call or lcall, indirect */
			fix_ip_or_call = UPROBE_FIX_CALL;
			break;
		case 4: case 5:			/* jmp or ljmp, indirect */
			fix_ip_or_call = 0;
			break;
		}
		/* fall through */
	default:
		riprel_analyze(auprobe, &insn);
	}

	auprobe->defparam.ilen = insn.length;
	auprobe->defparam.fixups |= fix_ip_or_call;

	auprobe->ops = &default_xol_ops;
	return 0;
}

/*
 * arch_uprobe_pre_xol - prepare to execute out of line.
 * @auprobe: the probepoint information.
 * @regs: reflects the saved user state of current task.
 */
int arch_uprobe_pre_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	if (auprobe->ops->pre_xol) {
		int err = auprobe->ops->pre_xol(auprobe, regs);
		if (err)
			return err;
	}

	regs->ip = utask->xol_vaddr;
	utask->autask.saved_trap_nr = current->thread.trap_nr;
	current->thread.trap_nr = UPROBE_TRAP_NR;

	utask->autask.saved_tf = !!(regs->flags & X86_EFLAGS_TF);
	regs->flags |= X86_EFLAGS_TF;
	if (test_tsk_thread_flag(current, TIF_BLOCKSTEP))
		set_task_blockstep(current, false);

	return 0;
}

/*
 * If xol insn itself traps and generates a signal(Say,
 * SIGILL/SIGSEGV/etc), then detect the case where a singlestepped
 * instruction jumps back to its own address. It is assumed that anything
 * like do_page_fault/do_trap/etc sets thread.trap_nr != -1.
 *
 * arch_uprobe_pre_xol/arch_uprobe_post_xol save/restore thread.trap_nr,
 * arch_uprobe_xol_was_trapped() simply checks that ->trap_nr is not equal to
 * UPROBE_TRAP_NR == -1 set by arch_uprobe_pre_xol().
 */
bool arch_uprobe_xol_was_trapped(struct task_struct *t)
{
	if (t->thread.trap_nr != UPROBE_TRAP_NR)
		return true;

	return false;
}

/*
 * Called after single-stepping. To avoid the SMP problems that can
 * occur when we temporarily put back the original opcode to
 * single-step, we single-stepped a copy of the instruction.
 *
 * This function prepares to resume execution after the single-step.
 */
int arch_uprobe_post_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;
	bool send_sigtrap = utask->autask.saved_tf;
	int err = 0;

	WARN_ON_ONCE(current->thread.trap_nr != UPROBE_TRAP_NR);
	current->thread.trap_nr = utask->autask.saved_trap_nr;

	if (auprobe->ops->post_xol) {
		err = auprobe->ops->post_xol(auprobe, regs);
		if (err) {
			/*
			 * Restore ->ip for restart or post mortem analysis.
			 * ->post_xol() must not return -ERESTART unless this
			 * is really possible.
			 */
			regs->ip = utask->vaddr;
			if (err == -ERESTART)
				err = 0;
			send_sigtrap = false;
		}
	}
	/*
	 * arch_uprobe_pre_xol() doesn't save the state of TIF_BLOCKSTEP
	 * so we can get an extra SIGTRAP if we do not clear TF. We need
	 * to examine the opcode to make it right.
	 */
	if (send_sigtrap)
		send_sig(SIGTRAP, current, 0);

	if (!utask->autask.saved_tf)
		regs->flags &= ~X86_EFLAGS_TF;

	return err;
}

/* callback routine for handling exceptions. */
int arch_uprobe_exception_notify(struct notifier_block *self, unsigned long val, void *data)
{
	struct die_args *args = data;
	struct pt_regs *regs = args->regs;
	int ret = NOTIFY_DONE;

	/* We are only interested in userspace traps */
	if (regs && !user_mode_vm(regs))
		return NOTIFY_DONE;

	switch (val) {
	case DIE_INT3:
		if (uprobe_pre_sstep_notifier(regs))
			ret = NOTIFY_STOP;

		break;

	case DIE_DEBUG:
		if (uprobe_post_sstep_notifier(regs))
			ret = NOTIFY_STOP;

	default:
		break;
	}

	return ret;
}

/*
 * This function gets called when XOL instruction either gets trapped or
 * the thread has a fatal signal. Reset the instruction pointer to its
 * probed address for the potential restart or for post mortem analysis.
 */
void arch_uprobe_abort_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	if (auprobe->ops->abort)
		auprobe->ops->abort(auprobe, regs);

	current->thread.trap_nr = utask->autask.saved_trap_nr;
	regs->ip = utask->vaddr;
	/* clear TF if it was set by us in arch_uprobe_pre_xol() */
	if (!utask->autask.saved_tf)
		regs->flags &= ~X86_EFLAGS_TF;
}

static bool __skip_sstep(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	if (auprobe->ops->emulate)
		return auprobe->ops->emulate(auprobe, regs);
	return false;
}

bool arch_uprobe_skip_sstep(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	bool ret = __skip_sstep(auprobe, regs);
	if (ret && (regs->flags & X86_EFLAGS_TF))
		send_sig(SIGTRAP, current, 0);
	return ret;
}

unsigned long
arch_uretprobe_hijack_return_addr(unsigned long trampoline_vaddr, struct pt_regs *regs)
{
	int rasize = sizeof_long(), nleft;
	unsigned long orig_ret_vaddr = 0; /* clear high bits for 32-bit apps */

	if (copy_from_user(&orig_ret_vaddr, (void __user *)regs->sp, rasize))
		return -1;

	/* check whether address has been already hijacked */
	if (orig_ret_vaddr == trampoline_vaddr)
		return orig_ret_vaddr;

	nleft = copy_to_user((void __user *)regs->sp, &trampoline_vaddr, rasize);
	if (likely(!nleft))
		return orig_ret_vaddr;

	if (nleft != rasize) {
		pr_err("uprobe: return address clobbered: pid=%d, %%sp=%#lx, "
			"%%ip=%#lx\n", current->pid, regs->sp, regs->ip);

		force_sig_info(SIGSEGV, SEND_SIG_FORCED, current);
	}

	return -1;
}
