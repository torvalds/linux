/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 ARM Ltd.
 */

#ifndef __ASM_RSI_H_
#define __ASM_RSI_H_

#include <linux/errno.h>
#include <linux/jump_label.h>
#include <asm/rsi_cmds.h>

DECLARE_STATIC_KEY_FALSE(rsi_present);

void __init arm64_rsi_init(void);

bool __arm64_is_protected_mmio(phys_addr_t base, size_t size);

static inline bool is_realm_world(void)
{
	return static_branch_unlikely(&rsi_present);
}

static inline int rsi_set_memory_range(phys_addr_t start, phys_addr_t end,
				       enum ripas state, unsigned long flags)
{
	unsigned long ret;
	phys_addr_t top;

	while (start != end) {
		ret = rsi_set_addr_range_state(start, end, state, flags, &top);
		if (ret || top < start || top > end)
			return -EINVAL;
		start = top;
	}

	return 0;
}

/*
 * Convert the specified range to RAM. Do not use this if you rely on the
 * contents of a page that may already be in RAM state.
 */
static inline int rsi_set_memory_range_protected(phys_addr_t start,
						 phys_addr_t end)
{
	return rsi_set_memory_range(start, end, RSI_RIPAS_RAM,
				    RSI_CHANGE_DESTROYED);
}

/*
 * Convert the specified range to RAM. Do not convert any pages that may have
 * been DESTROYED, without our permission.
 */
static inline int rsi_set_memory_range_protected_safe(phys_addr_t start,
						      phys_addr_t end)
{
	return rsi_set_memory_range(start, end, RSI_RIPAS_RAM,
				    RSI_NO_CHANGE_DESTROYED);
}

static inline int rsi_set_memory_range_shared(phys_addr_t start,
					      phys_addr_t end)
{
	return rsi_set_memory_range(start, end, RSI_RIPAS_EMPTY,
				    RSI_CHANGE_DESTROYED);
}
#endif /* __ASM_RSI_H_ */
