/*
 * arch/h8300/asm/include/flat.h -- uClinux flat-format executables
 */

#ifndef __H8300_FLAT_H__
#define __H8300_FLAT_H__

#include <asm/unaligned.h>

#define	flat_argvp_envp_on_stack()		1
#define	flat_old_ram_flag(flags)		1
#define	flat_reloc_valid(reloc, size)		((reloc) <= (size))
#define	flat_set_persistent(relval, p)		0

/*
 * on the H8 a couple of the relocations have an instruction in the
 * top byte.  As there can only be 24bits of address space,  we just
 * always preserve that 8bits at the top,  when it isn't an instruction
 * is is 0 (davidm@snapgear.com)
 */

#define	flat_get_relocate_addr(rel)		(rel & ~0x00000001)
static inline int flat_get_addr_from_rp(u32 __user *rp, u32 relval, u32 flags,
					u32 *addr, u32 *persistent)
{
	u32 val = get_unaligned((__force u32 *)rp);
	if (!(flags & FLAT_FLAG_GOTPIC))
		val &= 0x00ffffff;
	*addr = val;
	return 0;
}

static inline int flat_put_addr_at_rp(u32 __user *rp, u32 addr, u32 rel)
{
	u32 *p = (__force u32 *)rp;
	put_unaligned((addr & 0x00ffffff) | (*(char *)p << 24), p);
	return 0;
}

#endif /* __H8300_FLAT_H__ */
