// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/syscalls.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/cachectl.h>

SYSCALL_DEFINE3(cacheflush,
		void __user *, addr,
		unsigned long, bytes,
		int, cache)
{
	switch (cache) {
	case BCACHE:
	case DCACHE:
		dcache_wb_range((unsigned long)addr,
				(unsigned long)addr + bytes);
		if (cache != BCACHE)
			break;
		fallthrough;
	case ICACHE:
		flush_icache_mm_range(current->mm,
				(unsigned long)addr,
				(unsigned long)addr + bytes);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
