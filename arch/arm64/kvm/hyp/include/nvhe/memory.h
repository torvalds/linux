/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KVM_HYP_MEMORY_H
#define __KVM_HYP_MEMORY_H

#include <asm/page.h>

#include <linux/types.h>

extern s64 hyp_physvirt_offset;

#define __hyp_pa(virt)	((phys_addr_t)(virt) + hyp_physvirt_offset)
#define __hyp_va(phys)	((void *)((phys_addr_t)(phys) - hyp_physvirt_offset))

static inline void *hyp_phys_to_virt(phys_addr_t phys)
{
	return __hyp_va(phys);
}

static inline phys_addr_t hyp_virt_to_phys(void *addr)
{
	return __hyp_pa(addr);
}

#endif /* __KVM_HYP_MEMORY_H */
