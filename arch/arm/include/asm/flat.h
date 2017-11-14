/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/arm/include/asm/flat.h -- uClinux flat-format executables
 */

#ifndef __ARM_FLAT_H__
#define __ARM_FLAT_H__

#include <linux/uaccess.h>

#define	flat_argvp_envp_on_stack()		1
#define	flat_old_ram_flag(flags)		(flags)
#define	flat_reloc_valid(reloc, size)		((reloc) <= (size))

static inline int flat_get_addr_from_rp(u32 __user *rp, u32 relval, u32 flags,
					u32 *addr, u32 *persistent)
{
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	return copy_from_user(addr, rp, 4) ? -EFAULT : 0;
#else
	return get_user(*addr, rp);
#endif
}

static inline int flat_put_addr_at_rp(u32 __user *rp, u32 addr, u32 rel)
{
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	return copy_to_user(rp, &addr, 4) ? -EFAULT : 0;
#else
	return put_user(addr, rp);
#endif
}

#define	flat_get_relocate_addr(rel)		(rel)
#define	flat_set_persistent(relval, p)		0

#endif /* __ARM_FLAT_H__ */
