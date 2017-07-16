/*
 * Copyright 2007 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm_types.h>
#include <linux/flat.h>

#define FLAT_BFIN_RELOC_TYPE_16_BIT 0
#define FLAT_BFIN_RELOC_TYPE_16H_BIT 1
#define FLAT_BFIN_RELOC_TYPE_32_BIT 2

unsigned long bfin_get_addr_from_rp(u32 *ptr,
		u32 relval,
		u32 flags,
		u32 *persistent)
{
	unsigned short *usptr = (unsigned short *)ptr;
	int type = (relval >> 26) & 7;
	u32 val;

	switch (type) {
	case FLAT_BFIN_RELOC_TYPE_16_BIT:
	case FLAT_BFIN_RELOC_TYPE_16H_BIT:
		usptr = (unsigned short *)ptr;
		pr_debug("*usptr = %x", get_unaligned(usptr));
		val = get_unaligned(usptr);
		val += *persistent;
		break;

	case FLAT_BFIN_RELOC_TYPE_32_BIT:
		pr_debug("*ptr = %lx", get_unaligned(ptr));
		val = get_unaligned(ptr);
		break;

	default:
		pr_debug("BINFMT_FLAT: Unknown relocation type %x\n", type);
		return 0;
	}

	/*
	 * Stack-relative relocs contain the offset into the stack, we
	 * have to add the stack's start address here and return 1 from
	 * flat_addr_absolute to prevent the normal address calculations
	 */
	if (relval & (1 << 29))
		return val + current->mm->context.end_brk;

	if ((flags & FLAT_FLAG_GOTPIC) == 0)
		val = htonl(val);
	return val;
}
EXPORT_SYMBOL(bfin_get_addr_from_rp);

/*
 * Insert the address ADDR into the symbol reference at RP;
 * RELVAL is the raw relocation-table entry from which RP is derived
 */
void bfin_put_addr_at_rp(u32 *ptr, u32 addr, u32 relval)
{
	unsigned short *usptr = (unsigned short *)ptr;
	int type = (relval >> 26) & 7;

	switch (type) {
	case FLAT_BFIN_RELOC_TYPE_16_BIT:
		put_unaligned(addr, usptr);
		pr_debug("new value %x at %p", get_unaligned(usptr), usptr);
		break;

	case FLAT_BFIN_RELOC_TYPE_16H_BIT:
		put_unaligned(addr >> 16, usptr);
		pr_debug("new value %x", get_unaligned(usptr));
		break;

	case FLAT_BFIN_RELOC_TYPE_32_BIT:
		put_unaligned(addr, ptr);
		pr_debug("new ptr =%lx", get_unaligned(ptr));
		break;
	}
}
EXPORT_SYMBOL(bfin_put_addr_at_rp);
