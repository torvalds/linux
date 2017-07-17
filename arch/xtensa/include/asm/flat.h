#ifndef __ASM_XTENSA_FLAT_H
#define __ASM_XTENSA_FLAT_H

#include <asm/unaligned.h>

#define flat_argvp_envp_on_stack()			0
#define flat_old_ram_flag(flags)			(flags)
#define flat_reloc_valid(reloc, size)			((reloc) <= (size))
static inline int flat_get_addr_from_rp(u32 __user *rp, u32 relval, u32 flags,
					u32 *addr, u32 *persistent)
{
	*addr = get_unaligned((__force u32 *)rp);
	return 0;
}
static inline int flat_put_addr_at_rp(u32 __user *rp, u32 addr, u32 rel)
{
	put_unaligned(addr, (__force u32 *)rp);
	return 0;
}
#define flat_get_relocate_addr(rel)			(rel)
#define flat_set_persistent(relval, p)			0

#endif /* __ASM_XTENSA_FLAT_H */
