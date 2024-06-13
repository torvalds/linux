// SPDX-License-Identifier: GPL-2.0-only
/*
 * Routines for doing kexec-based kdump
 *
 * Copyright (C) 2017 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 */

#include <linux/crash_dump.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/uio.h>
#include <asm/memory.h>

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

/**
 * elfcorehdr_read - read from ELF core header
 * @buf: buffer where the data is placed
 * @count: number of bytes to read
 * @ppos: address in the memory
 *
 * This function reads @count bytes from elf core header which exists
 * on crash dump kernel's memory.
 */
ssize_t elfcorehdr_read(char *buf, size_t count, u64 *ppos)
{
	memcpy(buf, phys_to_virt((phys_addr_t)*ppos), count);
	*ppos += count;

	return count;
}
