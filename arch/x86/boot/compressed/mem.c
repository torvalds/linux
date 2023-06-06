// SPDX-License-Identifier: GPL-2.0-only

#include "error.h"

void arch_accept_memory(phys_addr_t start, phys_addr_t end)
{
	/* Platform-specific memory-acceptance call goes here */
	error("Cannot accept memory");
}
