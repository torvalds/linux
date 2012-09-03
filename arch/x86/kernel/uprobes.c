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

/* No fixup needed */
#define UPROBE_FIX_NONE		0x0

/* Adjust IP back to vicinity of actual insn */
#define UPROBE_FIX_IP		0x1

/* Adjust the return address of a call insn */
#define UPROBE_FIX_CALL	0x2

/* Instruction will modify TF, don't change it */
#define UPROBE_FIX_SETF	0x4

#define UPROBE_FIX_RIP_AX	0x8000
#define UPROBE_FIX_RIP_CX	0x4000

#define	UPROBE_TRAP_NR		UINT_MAX

/* Adaptations for mhiramat x86 decoder v14. */
#define OPCODE1(insn)		((insn)->opcode.bytes[0])
#define OPCODE2(insn)		((insn)->opcode.bytes[1])
#define OPCODE3(insn)		((insn)->opcode.bytes[2])
#define MODRM_REG(insn)		X86_MODRM_REG(insn->modrm.value)

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

#ifdef CONFIG_X86_64
/* Good-instruction tables for 64-bit apps */
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
#endif
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

static int validate_insn_32bits(struct arch_uprobe *auprobe, struct insn *insn)
{
	insn_init(insn, auprobe->insn, false);

	/* Skip good instruction prefixes; reject "bad" ones. */
	insn_get_opcode(insn);
	if (is_prefix_bad(insn))
		return -ENOTSUPP;

	if (test_bit(OPCODE1(insn), (unsigned long *)good_insns_32))
		return 0;

	if (insn->opcode.nbytes == 2) {
		if (test_bit(OPCODE2(insn), (unsigned long *)good_2byte_insns))
			return 0;
	}

	return -ENOTSUPP;
}

/*
 * Figure out which fixups arch_uprobe_post_xol() will need to perform, and
 * annotate arch_uprobe->fixups accordingly.  To start with,
 * arch_uprobe->fixups is either zero or it reflects rip-related fixups.
 */
static void prepare_fixups(struct arch_uprobe *auprobe, struct insn *insn)
{
	bool fix_ip = true, fix_call = false;	/* defaults */
	int reg;

	insn_get_opcode(insn);	/* should be a nop */

	switch (OPCODE1(insn)) {
	case 0x9d:
		/* popf */
		auprobe->fixups |= UPROBE_FIX_SETF;
		break;
	case 0xc3:		/* ret/lret */
	case 0xcb:
	case 0xc2:
	case 0xca:
		/* ip is correct */
		fix_ip = false;
		break;
	case 0xe8:		/* call relative - Fix return addr */
		fix_call = true;
		break;
	case 0x9a:		/* call absolute - Fix return addr, not ip */
		fix_call = true;
		fix_ip = false;
		break;
	case 0xff:
		insn_get_modrm(insn);
		reg = MODRM_REG(insn);
		if (reg == 2 || reg == 3) {
			/* call or lcall, indirect */
			/* Fix return addr; ip is correct. */
			fix_call = true;
			fix_ip = false;
		} else if (reg == 4 || reg == 5) {
			/* jmp or ljmp, indirect */
			/* ip is correct. */
			fix_ip = false;
		}
		break;
	case 0xea:		/* jmp absolute -- ip is correct */
		fix_ip = false;
		break;
	default:
		break;
	}
	if (fix_ip)
		auprobe->fixups |= UPROBE_FIX_IP;
	if (fix_call)
		auprobe->fixups |= UPROBE_FIX_CALL;
}

#ifdef CONFIG_X86_64
/*
 * If arch_uprobe->insn doesn't use rip-relative addressing, return
 * immediately.  Otherwise, rewrite the instruction so that it accesses
 * its memory operand indirectly through a scratch register.  Set
 * arch_uprobe->fixups and arch_uprobe->rip_rela_target_address
 * accordingly.  (The contents of the scratch register will be saved
 * before we single-step the modified instruction, and restored
 * afterward.)
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
 *  - There's always a modrm byte.
 *  - There's never a SIB byte.
 *  - The displacement is always 4 bytes.
 */
static void
handle_riprel_insn(struct arch_uprobe *auprobe, struct mm_struct *mm, struct insn *insn)
{
	u8 *cursor;
	u8 reg;

	if (mm->context.ia32_compat)
		return;

	auprobe->rip_rela_target_address = 0x0;
	if (!insn_rip_relative(insn))
		return;

	/*
	 * insn_rip_relative() would have decoded rex_prefix, modrm.
	 * Clear REX.b bit (extension of MODRM.rm field):
	 * we want to encode rax/rcx, not r8/r9.
	 */
	if (insn->rex_prefix.nbytes) {
		cursor = auprobe->insn + insn_offset_rex_prefix(insn);
		*cursor &= 0xfe;	/* Clearing REX.B bit */
	}

	/*
	 * Point cursor at the modrm byte.  The next 4 bytes are the
	 * displacement.  Beyond the displacement, for some instructions,
	 * is the immediate operand.
	 */
	cursor = auprobe->insn + insn_offset_modrm(insn);
	insn_get_length(insn);

	/*
	 * Convert from rip-relative addressing to indirect addressing
	 * via a scratch register.  Change the r/m field from 0x5 (%rip)
	 * to 0x0 (%rax) or 0x1 (%rcx), and squeeze out the offset field.
	 */
	reg = MODRM_REG(insn);
	if (reg == 0) {
		/*
		 * The register operand (if any) is either the A register
		 * (%rax, %eax, etc.) or (if the 0x4 bit is set in the
		 * REX prefix) %r8.  In any case, we know the C register
		 * is NOT the register operand, so we use %rcx (register
		 * #1) for the scratch register.
		 */
		auprobe->fixups = UPROBE_FIX_RIP_CX;
		/* Change modrm from 00 000 101 to 00 000 001. */
		*cursor = 0x1;
	} else {
		/* Use %rax (register #0) for the scratch register. */
		auprobe->fixups = UPROBE_FIX_RIP_AX;
		/* Change modrm from 00 xxx 101 to 00 xxx 000 */
		*cursor = (reg << 3);
	}

	/* Target address = address of next instruction + (signed) offset */
	auprobe->rip_rela_target_address = (long)insn->length + insn->displacement.value;

	/* Displacement field is gone; slide immediate field (if any) over. */
	if (insn->immediate.nbytes) {
		cursor++;
		memmove(cursor, cursor + insn->displacement.nbytes, insn->immediate.nbytes);
	}
	return;
}

static int validate_insn_64bits(struct arch_uprobe *auprobe, struct insn *insn)
{
	insn_init(insn, auprobe->insn, true);

	/* Skip good instruction prefixes; reject "bad" ones. */
	insn_get_opcode(insn);
	if (is_prefix_bad(insn))
		return -ENOTSUPP;

	if (test_bit(OPCODE1(insn), (unsigned long *)good_insns_64))
		return 0;

	if (insn->opcode.nbytes == 2) {
		if (test_bit(OPCODE2(insn), (unsigned long *)good_2byte_insns))
			return 0;
	}
	return -ENOTSUPP;
}

static int validate_insn_bits(struct arch_uprobe *auprobe, struct mm_struct *mm, struct insn *insn)
{
	if (mm->context.ia32_compat)
		return validate_insn_32bits(auprobe, insn);
	return validate_insn_64bits(auprobe, insn);
}
#else /* 32-bit: */
static void handle_riprel_insn(struct arch_uprobe *auprobe, struct mm_struct *mm, struct insn *insn)
{
	/* No RIP-relative addressing on 32-bit */
}

static int validate_insn_bits(struct arch_uprobe *auprobe, struct mm_struct *mm,  struct insn *insn)
{
	return validate_insn_32bits(auprobe, insn);
}
#endif /* CONFIG_X86_64 */

/**
 * arch_uprobe_analyze_insn - instruction analysis including validity and fixups.
 * @mm: the probed address space.
 * @arch_uprobe: the probepoint information.
 * @addr: virtual address at which to install the probepoint
 * Return 0 on success or a -ve number on error.
 */
int arch_uprobe_analyze_insn(struct arch_uprobe *auprobe, struct mm_struct *mm, unsigned long addr)
{
	int ret;
	struct insn insn;

	auprobe->fixups = 0;
	ret = validate_insn_bits(auprobe, mm, &insn);
	if (ret != 0)
		return ret;

	handle_riprel_insn(auprobe, mm, &insn);
	prepare_fixups(auprobe, &insn);

	return 0;
}

#ifdef CONFIG_X86_64
/*
 * If we're emulating a rip-relative instruction, save the contents
 * of the scratch register and store the target address in that register.
 */
static void
pre_xol_rip_insn(struct arch_uprobe *auprobe, struct pt_regs *regs,
				struct arch_uprobe_task *autask)
{
	if (auprobe->fixups & UPROBE_FIX_RIP_AX) {
		autask->saved_scratch_register = regs->ax;
		regs->ax = current->utask->vaddr;
		regs->ax += auprobe->rip_rela_target_address;
	} else if (auprobe->fixups & UPROBE_FIX_RIP_CX) {
		autask->saved_scratch_register = regs->cx;
		regs->cx = current->utask->vaddr;
		regs->cx += auprobe->rip_rela_target_address;
	}
}
#else
static void
pre_xol_rip_insn(struct arch_uprobe *auprobe, struct pt_regs *regs,
				struct arch_uprobe_task *autask)
{
	/* No RIP-relative addressing on 32-bit */
}
#endif

/*
 * arch_uprobe_pre_xol - prepare to execute out of line.
 * @auprobe: the probepoint information.
 * @regs: reflects the saved user state of current task.
 */
int arch_uprobe_pre_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct arch_uprobe_task *autask;

	autask = &current->utask->autask;
	autask->saved_trap_nr = current->thread.trap_nr;
	current->thread.trap_nr = UPROBE_TRAP_NR;
	regs->ip = current->utask->xol_vaddr;
	pre_xol_rip_insn(auprobe, regs, autask);

	return 0;
}

/*
 * This function is called by arch_uprobe_post_xol() to adjust the return
 * address pushed by a call instruction executed out of line.
 */
static int adjust_ret_addr(unsigned long sp, long correction)
{
	int rasize, ncopied;
	long ra = 0;

	if (is_ia32_task())
		rasize = 4;
	else
		rasize = 8;

	ncopied = copy_from_user(&ra, (void __user *)sp, rasize);
	if (unlikely(ncopied))
		return -EFAULT;

	ra += correction;
	ncopied = copy_to_user((void __user *)sp, &ra, rasize);
	if (unlikely(ncopied))
		return -EFAULT;

	return 0;
}

#ifdef CONFIG_X86_64
static bool is_riprel_insn(struct arch_uprobe *auprobe)
{
	return ((auprobe->fixups & (UPROBE_FIX_RIP_AX | UPROBE_FIX_RIP_CX)) != 0);
}

static void
handle_riprel_post_xol(struct arch_uprobe *auprobe, struct pt_regs *regs, long *correction)
{
	if (is_riprel_insn(auprobe)) {
		struct arch_uprobe_task *autask;

		autask = &current->utask->autask;
		if (auprobe->fixups & UPROBE_FIX_RIP_AX)
			regs->ax = autask->saved_scratch_register;
		else
			regs->cx = autask->saved_scratch_register;

		/*
		 * The original instruction includes a displacement, and so
		 * is 4 bytes longer than what we've just single-stepped.
		 * Fall through to handle stuff like "jmpq *...(%rip)" and
		 * "callq *...(%rip)".
		 */
		if (correction)
			*correction += 4;
	}
}
#else
static void
handle_riprel_post_xol(struct arch_uprobe *auprobe, struct pt_regs *regs, long *correction)
{
	/* No RIP-relative addressing on 32-bit */
}
#endif

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
 * instruction using a scratch register -- e.g., "movl %edx,(%rax)".
 * We need to restore the contents of the scratch register and adjust
 * the ip, keeping in mind that the instruction we executed is 4 bytes
 * shorter than the original instruction (since we squeezed out the offset
 * field).  (FIX_RIP_AX or FIX_RIP_CX)
 */
int arch_uprobe_post_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask;
	long correction;
	int result = 0;

	WARN_ON_ONCE(current->thread.trap_nr != UPROBE_TRAP_NR);

	utask = current->utask;
	current->thread.trap_nr = utask->autask.saved_trap_nr;
	correction = (long)(utask->vaddr - utask->xol_vaddr);
	handle_riprel_post_xol(auprobe, regs, &correction);
	if (auprobe->fixups & UPROBE_FIX_IP)
		regs->ip += correction;

	if (auprobe->fixups & UPROBE_FIX_CALL)
		result = adjust_ret_addr(regs->sp, correction);

	return result;
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
 * the thread has a fatal signal, so reset the instruction pointer to its
 * probed address.
 */
void arch_uprobe_abort_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	current->thread.trap_nr = utask->autask.saved_trap_nr;
	handle_riprel_post_xol(auprobe, regs, NULL);
	instruction_pointer_set(regs, utask->vaddr);
}

/*
 * Skip these instructions as per the currently known x86 ISA.
 * 0x66* { 0x90 | 0x0f 0x1f | 0x0f 0x19 | 0x87 0xc0 }
 */
bool arch_uprobe_skip_sstep(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	int i;

	for (i = 0; i < MAX_UINSN_BYTES; i++) {
		if ((auprobe->insn[i] == 0x66))
			continue;

		if (auprobe->insn[i] == 0x90)
			return true;

		if (i == (MAX_UINSN_BYTES - 1))
			break;

		if ((auprobe->insn[i] == 0x0f) && (auprobe->insn[i+1] == 0x1f))
			return true;

		if ((auprobe->insn[i] == 0x0f) && (auprobe->insn[i+1] == 0x19))
			return true;

		if ((auprobe->insn[i] == 0x87) && (auprobe->insn[i+1] == 0xc0))
			return true;

		break;
	}
	return false;
}

void arch_uprobe_enable_step(struct arch_uprobe *auprobe)
{
	struct task_struct *task = current;
	struct arch_uprobe_task	*autask	= &task->utask->autask;
	struct pt_regs *regs = task_pt_regs(task);

	autask->restore_flags = 0;
	if (!(regs->flags & X86_EFLAGS_TF) &&
	    !(auprobe->fixups & UPROBE_FIX_SETF))
		autask->restore_flags |= UPROBE_CLEAR_TF;

	regs->flags |= X86_EFLAGS_TF;
	if (test_tsk_thread_flag(task, TIF_BLOCKSTEP))
		set_task_blockstep(task, false);
}

void arch_uprobe_disable_step(struct arch_uprobe *auprobe)
{
	struct task_struct *task = current;
	struct arch_uprobe_task	*autask	= &task->utask->autask;
	struct pt_regs *regs = task_pt_regs(task);
	/*
	 * The state of TIF_BLOCKSTEP was not saved so we can get an extra
	 * SIGTRAP if we do not clear TF. We need to examine the opcode to
	 * make it right.
	 */
	if (autask->restore_flags & UPROBE_CLEAR_TF)
		regs->flags &= ~X86_EFLAGS_TF;
}
