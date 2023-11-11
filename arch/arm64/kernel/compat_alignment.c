// SPDX-License-Identifier: GPL-2.0-only
// based on arch/arm/mm/alignment.c

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/perf_event.h>
#include <linux/uaccess.h>

#include <asm/exception.h>
#include <asm/ptrace.h>
#include <asm/traps.h>

/*
 * 32-bit misaligned trap handler (c) 1998 San Mehat (CCC) -July 1998
 *
 * Speed optimisations and better fault handling by Russell King.
 */
#define CODING_BITS(i)	(i & 0x0e000000)

#define LDST_P_BIT(i)	(i & (1 << 24))		/* Preindex		*/
#define LDST_U_BIT(i)	(i & (1 << 23))		/* Add offset		*/
#define LDST_W_BIT(i)	(i & (1 << 21))		/* Writeback		*/
#define LDST_L_BIT(i)	(i & (1 << 20))		/* Load			*/

#define LDST_P_EQ_U(i)	((((i) ^ ((i) >> 1)) & (1 << 23)) == 0)

#define LDSTHD_I_BIT(i)	(i & (1 << 22))		/* double/half-word immed */

#define RN_BITS(i)	((i >> 16) & 15)	/* Rn			*/
#define RD_BITS(i)	((i >> 12) & 15)	/* Rd			*/
#define RM_BITS(i)	(i & 15)		/* Rm			*/

#define REGMASK_BITS(i)	(i & 0xffff)

#define BAD_INSTR 	0xdeadc0de

/* Thumb-2 32 bit format per ARMv7 DDI0406A A6.3, either f800h,e800h,f800h */
#define IS_T32(hi16) \
	(((hi16) & 0xe000) == 0xe000 && ((hi16) & 0x1800))

union offset_union {
	unsigned long un;
	  signed long sn;
};

#define TYPE_ERROR	0
#define TYPE_FAULT	1
#define TYPE_LDST	2
#define TYPE_DONE	3

static void
do_alignment_finish_ldst(unsigned long addr, u32 instr, struct pt_regs *regs,
			 union offset_union offset)
{
	if (!LDST_U_BIT(instr))
		offset.un = -offset.un;

	if (!LDST_P_BIT(instr))
		addr += offset.un;

	if (!LDST_P_BIT(instr) || LDST_W_BIT(instr))
		regs->regs[RN_BITS(instr)] = addr;
}

static int
do_alignment_ldrdstrd(unsigned long addr, u32 instr, struct pt_regs *regs)
{
	unsigned int rd = RD_BITS(instr);
	unsigned int rd2;
	int load;

	if ((instr & 0xfe000000) == 0xe8000000) {
		/* ARMv7 Thumb-2 32-bit LDRD/STRD */
		rd2 = (instr >> 8) & 0xf;
		load = !!(LDST_L_BIT(instr));
	} else if (((rd & 1) == 1) || (rd == 14)) {
		return TYPE_ERROR;
	} else {
		load = ((instr & 0xf0) == 0xd0);
		rd2 = rd + 1;
	}

	if (load) {
		unsigned int val, val2;

		if (get_user(val, (u32 __user *)addr) ||
		    get_user(val2, (u32 __user *)(addr + 4)))
			return TYPE_FAULT;
		regs->regs[rd] = val;
		regs->regs[rd2] = val2;
	} else {
		if (put_user(regs->regs[rd], (u32 __user *)addr) ||
		    put_user(regs->regs[rd2], (u32 __user *)(addr + 4)))
			return TYPE_FAULT;
	}
	return TYPE_LDST;
}

/*
 * LDM/STM alignment handler.
 *
 * There are 4 variants of this instruction:
 *
 * B = rn pointer before instruction, A = rn pointer after instruction
 *              ------ increasing address ----->
 *	        |    | r0 | r1 | ... | rx |    |
 * PU = 01             B                    A
 * PU = 11        B                    A
 * PU = 00        A                    B
 * PU = 10             A                    B
 */
static int
do_alignment_ldmstm(unsigned long addr, u32 instr, struct pt_regs *regs)
{
	unsigned int rd, rn, nr_regs, regbits;
	unsigned long eaddr, newaddr;
	unsigned int val;

	/* count the number of registers in the mask to be transferred */
	nr_regs = hweight16(REGMASK_BITS(instr)) * 4;

	rn = RN_BITS(instr);
	newaddr = eaddr = regs->regs[rn];

	if (!LDST_U_BIT(instr))
		nr_regs = -nr_regs;
	newaddr += nr_regs;
	if (!LDST_U_BIT(instr))
		eaddr = newaddr;

	if (LDST_P_EQ_U(instr))	/* U = P */
		eaddr += 4;

	for (regbits = REGMASK_BITS(instr), rd = 0; regbits;
	     regbits >>= 1, rd += 1)
		if (regbits & 1) {
			if (LDST_L_BIT(instr)) {
				if (get_user(val, (u32 __user *)eaddr))
					return TYPE_FAULT;
				if (rd < 15)
					regs->regs[rd] = val;
				else
					regs->pc = val;
			} else {
				/*
				 * The PC register has a bias of +8 in ARM mode
				 * and +4 in Thumb mode. This means that a read
				 * of the value of PC should account for this.
				 * Since Thumb does not permit STM instructions
				 * to refer to PC, just add 8 here.
				 */
				val = (rd < 15) ? regs->regs[rd] : regs->pc + 8;
				if (put_user(val, (u32 __user *)eaddr))
					return TYPE_FAULT;
			}
			eaddr += 4;
		}

	if (LDST_W_BIT(instr))
		regs->regs[rn] = newaddr;

	return TYPE_DONE;
}

/*
 * Convert Thumb multi-word load/store instruction forms to equivalent ARM
 * instructions so we can reuse ARM userland alignment fault fixups for Thumb.
 *
 * This implementation was initially based on the algorithm found in
 * gdb/sim/arm/thumbemu.c. It is basically just a code reduction of same
 * to convert only Thumb ld/st instruction forms to equivalent ARM forms.
 *
 * NOTES:
 * 1. Comments below refer to ARM ARM DDI0100E Thumb Instruction sections.
 * 2. If for some reason we're passed an non-ld/st Thumb instruction to
 *    decode, we return 0xdeadc0de. This should never happen under normal
 *    circumstances but if it does, we've got other problems to deal with
 *    elsewhere and we obviously can't fix those problems here.
 */

static unsigned long thumb2arm(u16 tinstr)
{
	u32 L = (tinstr & (1<<11)) >> 11;

	switch ((tinstr & 0xf800) >> 11) {
	/* 6.6.1 Format 1: */
	case 0xc000 >> 11:				/* 7.1.51 STMIA */
	case 0xc800 >> 11:				/* 7.1.25 LDMIA */
		{
			u32 Rn = (tinstr & (7<<8)) >> 8;
			u32 W = ((L<<Rn) & (tinstr&255)) ? 0 : 1<<21;

			return 0xe8800000 | W | (L<<20) | (Rn<<16) |
				(tinstr&255);
		}

	/* 6.6.1 Format 2: */
	case 0xb000 >> 11:				/* 7.1.48 PUSH */
	case 0xb800 >> 11:				/* 7.1.47 POP */
		if ((tinstr & (3 << 9)) == 0x0400) {
			static const u32 subset[4] = {
				0xe92d0000,	/* STMDB sp!,{registers} */
				0xe92d4000,	/* STMDB sp!,{registers,lr} */
				0xe8bd0000,	/* LDMIA sp!,{registers} */
				0xe8bd8000	/* LDMIA sp!,{registers,pc} */
			};
			return subset[(L<<1) | ((tinstr & (1<<8)) >> 8)] |
			    (tinstr & 255);		/* register_list */
		}
		fallthrough;	/* for illegal instruction case */

	default:
		return BAD_INSTR;
	}
}

/*
 * Convert Thumb-2 32 bit LDM, STM, LDRD, STRD to equivalent instruction
 * handlable by ARM alignment handler, also find the corresponding handler,
 * so that we can reuse ARM userland alignment fault fixups for Thumb.
 *
 * @pinstr: original Thumb-2 instruction; returns new handlable instruction
 * @regs: register context.
 * @poffset: return offset from faulted addr for later writeback
 *
 * NOTES:
 * 1. Comments below refer to ARMv7 DDI0406A Thumb Instruction sections.
 * 2. Register name Rt from ARMv7 is same as Rd from ARMv6 (Rd is Rt)
 */
static void *
do_alignment_t32_to_handler(u32 *pinstr, struct pt_regs *regs,
			    union offset_union *poffset)
{
	u32 instr = *pinstr;
	u16 tinst1 = (instr >> 16) & 0xffff;
	u16 tinst2 = instr & 0xffff;

	switch (tinst1 & 0xffe0) {
	/* A6.3.5 Load/Store multiple */
	case 0xe880:		/* STM/STMIA/STMEA,LDM/LDMIA, PUSH/POP T2 */
	case 0xe8a0:		/* ...above writeback version */
	case 0xe900:		/* STMDB/STMFD, LDMDB/LDMEA */
	case 0xe920:		/* ...above writeback version */
		/* no need offset decision since handler calculates it */
		return do_alignment_ldmstm;

	case 0xf840:		/* POP/PUSH T3 (single register) */
		if (RN_BITS(instr) == 13 && (tinst2 & 0x09ff) == 0x0904) {
			u32 L = !!(LDST_L_BIT(instr));
			const u32 subset[2] = {
				0xe92d0000,	/* STMDB sp!,{registers} */
				0xe8bd0000,	/* LDMIA sp!,{registers} */
			};
			*pinstr = subset[L] | (1<<RD_BITS(instr));
			return do_alignment_ldmstm;
		}
		/* Else fall through for illegal instruction case */
		break;

	/* A6.3.6 Load/store double, STRD/LDRD(immed, lit, reg) */
	case 0xe860:
	case 0xe960:
	case 0xe8e0:
	case 0xe9e0:
		poffset->un = (tinst2 & 0xff) << 2;
		fallthrough;

	case 0xe940:
	case 0xe9c0:
		return do_alignment_ldrdstrd;

	/*
	 * No need to handle load/store instructions up to word size
	 * since ARMv6 and later CPUs can perform unaligned accesses.
	 */
	default:
		break;
	}
	return NULL;
}

static int alignment_get_arm(struct pt_regs *regs, __le32 __user *ip, u32 *inst)
{
	__le32 instr = 0;
	int fault;

	fault = get_user(instr, ip);
	if (fault)
		return fault;

	*inst = __le32_to_cpu(instr);
	return 0;
}

static int alignment_get_thumb(struct pt_regs *regs, __le16 __user *ip, u16 *inst)
{
	__le16 instr = 0;
	int fault;

	fault = get_user(instr, ip);
	if (fault)
		return fault;

	*inst = __le16_to_cpu(instr);
	return 0;
}

int do_compat_alignment_fixup(unsigned long addr, struct pt_regs *regs)
{
	union offset_union offset;
	unsigned long instrptr;
	int (*handler)(unsigned long addr, u32 instr, struct pt_regs *regs);
	unsigned int type;
	u32 instr = 0;
	int isize = 4;
	int thumb2_32b = 0;

	instrptr = instruction_pointer(regs);

	if (compat_thumb_mode(regs)) {
		__le16 __user *ptr = (__le16 __user *)(instrptr & ~1);
		u16 tinstr, tinst2;

		if (alignment_get_thumb(regs, ptr, &tinstr))
			return 1;

		if (IS_T32(tinstr)) { /* Thumb-2 32-bit */
			if (alignment_get_thumb(regs, ptr + 1, &tinst2))
				return 1;
			instr = ((u32)tinstr << 16) | tinst2;
			thumb2_32b = 1;
		} else {
			isize = 2;
			instr = thumb2arm(tinstr);
		}
	} else {
		if (alignment_get_arm(regs, (__le32 __user *)instrptr, &instr))
			return 1;
	}

	switch (CODING_BITS(instr)) {
	case 0x00000000:	/* 3.13.4 load/store instruction extensions */
		if (LDSTHD_I_BIT(instr))
			offset.un = (instr & 0xf00) >> 4 | (instr & 15);
		else
			offset.un = regs->regs[RM_BITS(instr)];

		if ((instr & 0x001000f0) == 0x000000d0 || /* LDRD */
		    (instr & 0x001000f0) == 0x000000f0)   /* STRD */
			handler = do_alignment_ldrdstrd;
		else
			return 1;
		break;

	case 0x08000000:	/* ldm or stm, or thumb-2 32bit instruction */
		if (thumb2_32b) {
			offset.un = 0;
			handler = do_alignment_t32_to_handler(&instr, regs, &offset);
		} else {
			offset.un = 0;
			handler = do_alignment_ldmstm;
		}
		break;

	default:
		return 1;
	}

	type = handler(addr, instr, regs);

	if (type == TYPE_ERROR || type == TYPE_FAULT)
		return 1;

	if (type == TYPE_LDST)
		do_alignment_finish_ldst(addr, instr, regs, offset);

	perf_sw_event(PERF_COUNT_SW_ALIGNMENT_FAULTS, 1, regs, regs->pc);
	arm64_skip_faulting_instruction(regs, isize);

	return 0;
}
