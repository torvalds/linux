// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/syscalls.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/cachectl.h>

SYSCALL_DEFINE3(cacheflush,
		void __user *, addr,
		unsigned long, bytes,
		int, cache)
{
	switch (cache) {
	case ICACHE:
		icache_inv_range((unsigned long)addr,
				 (unsigned long)addr + bytes);
		break;
	case DCACHE:
		dcache_wb_range((unsigned long)addr,
				(unsigned long)addr + bytes);
		break;
	case BCACHE:
		cache_wbinv_range((unsigned long)addr,
				  (unsigned long)addr + bytes);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
