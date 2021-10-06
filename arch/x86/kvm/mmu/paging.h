/* SPDX-License-Identifier: GPL-2.0-only */
/* Shadow paging constants/helpers that don't need to be #undef'd. */
#ifndef __KVM_X86_PAGING_H
#define __KVM_X86_PAGING_H

#define GUEST_PT64_BASE_ADDR_MASK (((1ULL << 52) - 1) & ~(u64)(PAGE_SIZE-1))
#define PT64_LVL_ADDR_MASK(level) \
	(GUEST_PT64_BASE_ADDR_MASK & ~((1ULL << (PAGE_SHIFT + (((level) - 1) \
						* PT64_LEVEL_BITS))) - 1))
#define PT64_LVL_OFFSET_MASK(level) \
	(GUEST_PT64_BASE_ADDR_MASK & ((1ULL << (PAGE_SHIFT + (((level) - 1) \
						* PT64_LEVEL_BITS))) - 1))
#endif /* __KVM_X86_PAGING_H */

