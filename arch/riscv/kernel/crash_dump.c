// SPDX-License-Identifier: GPL-2.0
/*
 * This code comes from arch/arm64/kernel/crash_dump.c
 * Created by: AKASHI Takahiro <takahiro.akashi@linaro.org>
 * Copyright (C) 2017 Linaro Limited
 */

#include <linux/crash_dump.h>
#include <linux/io.h>
#include <linux/uio.h>

ssize_t copy_oldmem_page(struct iov_iter *iter, unsigned long pfn,
			 size_t csize, unsigned long offset)
{
	void *vaddr;

	if (!csize)
		return 0;

	vaddr = memremap(__pfn_to_phys(pfn), PAGE_SIZE, MEMREMAP_WB);
	if (!vaddr)
		return -ENOMEM;

	csize = copy_to_iter(vaddr + offset, csize, iter);

	memunmap(vaddr);
	return csize;
}
