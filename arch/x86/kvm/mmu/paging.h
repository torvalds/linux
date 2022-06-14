/* SPDX-License-Identifier: GPL-2.0-only */
/* Shadow paging constants/helpers that don't need to be #undef'd. */
#ifndef __KVM_X86_PAGING_H
#define __KVM_X86_PAGING_H

#define GUEST_PT64_BASE_ADDR_MASK (((1ULL << 52) - 1) & ~(u64)(PAGE_SIZE-1))

#define PT64_LVL_ADDR_MASK(level) \
	__PT_LVL_ADDR_MASK(GUEST_PT64_BASE_ADDR_MASK, level, PT64_LEVEL_BITS)

#define PT64_LVL_OFFSET_MASK(level) \
	__PT_LVL_OFFSET_MASK(GUEST_PT64_BASE_ADDR_MASK, level, PT64_LEVEL_BITS)

#endif /* __KVM_X86_PAGING_H */

