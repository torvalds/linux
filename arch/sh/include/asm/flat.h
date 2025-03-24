/* SPDX-License-Identifier: GPL-2.0
 *
 * include/asm-sh/flat.h
 *
 * uClinux flat-format executables
 *
 * Copyright (C) 2003  Paul Mundt
 */
#ifndef __ASM_SH_FLAT_H
#define __ASM_SH_FLAT_H

#include <linux/unaligned.h>

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

#define FLAT_PLAT_INIT(_r) \
  do { _r->regs[0]=0; _r->regs[1]=0; _r->regs[2]=0; _r->regs[3]=0; \
       _r->regs[4]=0; _r->regs[5]=0; _r->regs[6]=0; _r->regs[7]=0; \
       _r->regs[8]=0; _r->regs[9]=0; _r->regs[10]=0; _r->regs[11]=0; \
       _r->regs[12]=0; _r->regs[13]=0; _r->regs[14]=0; \
       _r->sr = SR_FD; } while (0)

#endif /* __ASM_SH_FLAT_H */
