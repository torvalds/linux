/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_XTENSA_FLAT_H
#define __ASM_XTENSA_FLAT_H

#include <asm/unaligned.h>

static inline int flat_get_addr_from_rp(u32 __user *rp, u32 relval, u32 flags,
					u32 *addr)
{
	*addr = get_unaligned((__force u32 *)rp);
	return 0;
}
static inline int flat_put_addr_at_rp(u32 __user *rp, u32 addr, u32 rel)
{
	put_unaligned(addr, (__force u32 *)rp);
	return 0;
}

#endif /* __ASM_XTENSA_FLAT_H */
