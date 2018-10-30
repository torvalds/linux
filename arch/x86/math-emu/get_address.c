// SPDX-License-Identifier: GPL-2.0
/*---------------------------------------------------------------------------+
 |  get_address.c                                                            |
 |                                                                           |
 | Get the effective address from an FPU instruction.                        |
 |                                                                           |
 | Copyright (C) 1992,1993,1994,1997                                         |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail   billm@suburbia.net             |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------+
 | Note:                                                                     |
 |    The file contains code which accesses user memory.                     |
 |    Emulator static data may change when user memory is accessed, due to   |
 |    other processes using the emulator while swapping is in progress.      |
 +---------------------------------------------------------------------------*/

#include <linux/stddef.h>

#include <linux/uaccess.h>
#include <asm/vm86.h>

#include "fpu_system.h"
#include "exception.h"
#include "fpu_emu.h"

#define FPU_WRITE_BIT 0x10

static int reg_offset[] = {
	offsetof(struct pt_regs, ax),
	offsetof(struct pt_regs, cx),
	offsetof(struct pt_regs, dx),
	offsetof(struct pt_regs, bx),
	offsetof(struct pt_regs, sp),
	offsetof(struct pt_regs, bp),
	offsetof(struct pt_regs, si),
	offsetof(struct pt_regs, di)
};

#define REG_(x) (*(long *)(reg_offset[(x)] + (u_char *)FPU_info->regs))

static int reg_offset_vm86[] = {
	offsetof(struct pt_regs, cs),
	offsetof(struct kernel_vm86_regs, ds),
	offsetof(struct kernel_vm86_regs, es),
	offsetof(struct kernel_vm86_regs, fs),
	offsetof(struct kernel_vm86_regs, gs),
	offsetof(struct pt_regs, ss),
	offsetof(struct kernel_vm86_regs, ds)
};

#define VM86_REG_(x) (*(unsigned short *) \
		(reg_offset_vm86[((unsigned)x)] + (u_char *)FPU_info->regs))

static int reg_offset_pm[] = {
	offsetof(struct pt_regs, cs),
	offsetof(struct pt_regs, ds),
	offsetof(struct pt_regs, es),
	offsetof(struct pt_regs, fs),
	offsetof(struct pt_regs, ds),	/* dummy, not saved on stack */
	offsetof(struct pt_regs, ss),
	offsetof(struct pt_regs, ds)
};

#define PM_REG_(x) (*(unsigned short *) \
		(reg_offset_pm[((unsigned)x)] + (u_char *)FPU_info->regs))

/* Decode the SIB byte. This function assumes mod != 0 */
static int sib(int mod, unsigned long *fpu_eip)
{
	u_char ss, index, base;
	long offset;

	RE_ENTRANT_CHECK_OFF;
	FPU_code_access_ok(1);
	FPU_get_user(base, (u_char __user *) (*fpu_eip));	/* The SIB byte */
	RE_ENTRANT_CHECK_ON;
	(*fpu_eip)++;
	ss = base >> 6;
	index = (base >> 3) & 7;
	base &= 7;

	if ((mod == 0) && (base == 5))
		offset = 0;	/* No base register */
	else
		offset = REG_(base);

	if (index == 4) {
		/* No index register */
		/* A non-zero ss is illegal */
		if (ss)
			EXCEPTION(EX_Invalid);
	} else {
		offset += (REG_(index)) << ss;
	}

	if (mod == 1) {
		/* 8 bit signed displacement */
		long displacement;
		RE_ENTRANT_CHECK_OFF;
		FPU_code_access_ok(1);
		FPU_get_user(displacement, (signed char __user *)(*fpu_eip));
		offset += displacement;
		RE_ENTRANT_CHECK_ON;
		(*fpu_eip)++;
	} else if (mod == 2 || base == 5) {	/* The second condition also has mod==0 */
		/* 32 bit displacement */
		long displacement;
		RE_ENTRANT_CHECK_OFF;
		FPU_code_access_ok(4);
		FPU_get_user(displacement, (long __user *)(*fpu_eip));
		offset += displacement;
		RE_ENTRANT_CHECK_ON;
		(*fpu_eip) += 4;
	}

	return offset;
}

static unsigned long vm86_segment(u_char segment, struct address *addr)
{
	segment--;
#ifdef PARANOID
	if (segment > PREFIX_SS_) {
		EXCEPTION(EX_INTERNAL | 0x130);
		math_abort(FPU_info, SIGSEGV);
	}
#endif /* PARANOID */
	addr->selector = VM86_REG_(segment);
	return (unsigned long)VM86_REG_(segment) << 4;
}

/* This should work for 16 and 32 bit protected mode. */
static long pm_address(u_char FPU_modrm, u_char segment,
		       struct address *addr, long offset)
{
	struct desc_struct descriptor;
	unsigned long base_address, limit, address, seg_top;

	segment--;

#ifdef PARANOID
	/* segment is unsigned, so this also detects if segment was 0: */
	if (segment > PREFIX_SS_) {
		EXCEPTION(EX_INTERNAL | 0x132);
		math_abort(FPU_info, SIGSEGV);
	}
#endif /* PARANOID */

	switch (segment) {
	case PREFIX_GS_ - 1:
		/* user gs handling can be lazy, use special accessors */
		addr->selector = get_user_gs(FPU_info->regs);
		break;
	default:
		addr->selector = PM_REG_(segment);
	}

	descriptor = FPU_get_ldt_descriptor(addr->selector);
	base_address = seg_get_base(&descriptor);
	address = base_address + offset;
	limit = seg_get_limit(&descriptor) + 1;
	limit *= seg_get_granularity(&descriptor);
	limit += base_address - 1;
	if (limit < base_address)
		limit = 0xffffffff;

	if (seg_expands_down(&descriptor)) {
		if (descriptor.g) {
			seg_top = 0xffffffff;
		} else {
			seg_top = base_address + (1 << 20);
			if (seg_top < base_address)
				seg_top = 0xffffffff;
		}
		access_limit =
		    (address <= limit) || (address >= seg_top) ? 0 :
		    ((seg_top - address) >= 255 ? 255 : seg_top - address);
	} else {
		access_limit =
		    (address > limit) || (address < base_address) ? 0 :
		    ((limit - address) >= 254 ? 255 : limit - address + 1);
	}
	if (seg_execute_only(&descriptor) ||
	    (!seg_writable(&descriptor) && (FPU_modrm & FPU_WRITE_BIT))) {
		access_limit = 0;
	}
	return address;
}

/*
       MOD R/M byte:  MOD == 3 has a special use for the FPU
                      SIB byte used iff R/M = 100b

       7   6   5   4   3   2   1   0
       .....   .........   .........
        MOD    OPCODE(2)     R/M

       SIB byte

       7   6   5   4   3   2   1   0
       .....   .........   .........
        SS      INDEX        BASE

*/

void __user *FPU_get_address(u_char FPU_modrm, unsigned long *fpu_eip,
			     struct address *addr, fpu_addr_modes addr_modes)
{
	u_char mod;
	unsigned rm = FPU_modrm & 7;
	long *cpu_reg_ptr;
	int address = 0;	/* Initialized just to stop compiler warnings. */

	/* Memory accessed via the cs selector is write protected
	   in `non-segmented' 32 bit protected mode. */
	if (!addr_modes.default_mode && (FPU_modrm & FPU_WRITE_BIT)
	    && (addr_modes.override.segment == PREFIX_CS_)) {
		math_abort(FPU_info, SIGSEGV);
	}

	addr->selector = FPU_DS;	/* Default, for 32 bit non-segmented mode. */

	mod = (FPU_modrm >> 6) & 3;

	if (rm == 4 && mod != 3) {
		address = sib(mod, fpu_eip);
	} else {
		cpu_reg_ptr = &REG_(rm);
		switch (mod) {
		case 0:
			if (rm == 5) {
				/* Special case: disp32 */
				RE_ENTRANT_CHECK_OFF;
				FPU_code_access_ok(4);
				FPU_get_user(address,
					     (unsigned long __user
					      *)(*fpu_eip));
				(*fpu_eip) += 4;
				RE_ENTRANT_CHECK_ON;
				addr->offset = address;
				return (void __user *)address;
			} else {
				address = *cpu_reg_ptr;	/* Just return the contents
							   of the cpu register */
				addr->offset = address;
				return (void __user *)address;
			}
		case 1:
			/* 8 bit signed displacement */
			RE_ENTRANT_CHECK_OFF;
			FPU_code_access_ok(1);
			FPU_get_user(address, (signed char __user *)(*fpu_eip));
			RE_ENTRANT_CHECK_ON;
			(*fpu_eip)++;
			break;
		case 2:
			/* 32 bit displacement */
			RE_ENTRANT_CHECK_OFF;
			FPU_code_access_ok(4);
			FPU_get_user(address, (long __user *)(*fpu_eip));
			(*fpu_eip) += 4;
			RE_ENTRANT_CHECK_ON;
			break;
		case 3:
			/* Not legal for the FPU */
			EXCEPTION(EX_Invalid);
		}
		address += *cpu_reg_ptr;
	}

	addr->offset = address;

	switch (addr_modes.default_mode) {
	case 0:
		break;
	case VM86:
		address += vm86_segment(addr_modes.override.segment, addr);
		break;
	case PM16:
	case SEG32:
		address = pm_address(FPU_modrm, addr_modes.override.segment,
				     addr, address);
		break;
	default:
		EXCEPTION(EX_INTERNAL | 0x133);
	}

	return (void __user *)address;
}

void __user *FPU_get_address_16(u_char FPU_modrm, unsigned long *fpu_eip,
				struct address *addr, fpu_addr_modes addr_modes)
{
	u_char mod;
	unsigned rm = FPU_modrm & 7;
	int address = 0;	/* Default used for mod == 0 */

	/* Memory accessed via the cs selector is write protected
	   in `non-segmented' 32 bit protected mode. */
	if (!addr_modes.default_mode && (FPU_modrm & FPU_WRITE_BIT)
	    && (addr_modes.override.segment == PREFIX_CS_)) {
		math_abort(FPU_info, SIGSEGV);
	}

	addr->selector = FPU_DS;	/* Default, for 32 bit non-segmented mode. */

	mod = (FPU_modrm >> 6) & 3;

	switch (mod) {
	case 0:
		if (rm == 6) {
			/* Special case: disp16 */
			RE_ENTRANT_CHECK_OFF;
			FPU_code_access_ok(2);
			FPU_get_user(address,
				     (unsigned short __user *)(*fpu_eip));
			(*fpu_eip) += 2;
			RE_ENTRANT_CHECK_ON;
			goto add_segment;
		}
		break;
	case 1:
		/* 8 bit signed displacement */
		RE_ENTRANT_CHECK_OFF;
		FPU_code_access_ok(1);
		FPU_get_user(address, (signed char __user *)(*fpu_eip));
		RE_ENTRANT_CHECK_ON;
		(*fpu_eip)++;
		break;
	case 2:
		/* 16 bit displacement */
		RE_ENTRANT_CHECK_OFF;
		FPU_code_access_ok(2);
		FPU_get_user(address, (unsigned short __user *)(*fpu_eip));
		(*fpu_eip) += 2;
		RE_ENTRANT_CHECK_ON;
		break;
	case 3:
		/* Not legal for the FPU */
		EXCEPTION(EX_Invalid);
		break;
	}
	switch (rm) {
	case 0:
		address += FPU_info->regs->bx + FPU_info->regs->si;
		break;
	case 1:
		address += FPU_info->regs->bx + FPU_info->regs->di;
		break;
	case 2:
		address += FPU_info->regs->bp + FPU_info->regs->si;
		if (addr_modes.override.segment == PREFIX_DEFAULT)
			addr_modes.override.segment = PREFIX_SS_;
		break;
	case 3:
		address += FPU_info->regs->bp + FPU_info->regs->di;
		if (addr_modes.override.segment == PREFIX_DEFAULT)
			addr_modes.override.segment = PREFIX_SS_;
		break;
	case 4:
		address += FPU_info->regs->si;
		break;
	case 5:
		address += FPU_info->regs->di;
		break;
	case 6:
		address += FPU_info->regs->bp;
		if (addr_modes.override.segment == PREFIX_DEFAULT)
			addr_modes.override.segment = PREFIX_SS_;
		break;
	case 7:
		address += FPU_info->regs->bx;
		break;
	}

      add_segment:
	address &= 0xffff;

	addr->offset = address;

	switch (addr_modes.default_mode) {
	case 0:
		break;
	case VM86:
		address += vm86_segment(addr_modes.override.segment, addr);
		break;
	case PM16:
	case SEG32:
		address = pm_address(FPU_modrm, addr_modes.override.segment,
				     addr, address);
		break;
	default:
		EXCEPTION(EX_INTERNAL | 0x131);
	}

	return (void __user *)address;
}
