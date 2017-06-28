/*
 * Routines for doing kexec-based kdump
 *
 * Copyright (C) 2017 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/crash_dump.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/uaccess.h>
#include <asm/memory.h>

/**
 * copy_oldmem_page() - copy one page from old kernel memory
 * @pfn: page frame number to be copied
 * @buf: buffer where the copied page is placed
 * @csize: number of bytes to copy
 * @offset: offset in bytes into the page
 * @userbuf: if set, @buf is in a user address space
 *
 * This function copies one page from old kernel memory into buffer pointed by
 * @buf. If @buf is in userspace, set @userbuf to %1. Returns number of bytes
 * copied or negative error in case of failure.
 */
ssize_t copy_oldmem_page(unsigned long pfn, char *buf,
			 size_t csize, unsigned long offset,
			 int userbuf)
{
	void *vaddr;

	if (!csize)
		return 0;

	vaddr = memremap(__pfn_to_phys(pfn), PAGE_SIZE, MEMREMAP_WB);
	if (!vaddr)
		return -ENOMEM;

	if (userbuf) {
		if (copy_to_user((char __user *)buf, vaddr + offset, csize)) {
			memunmap(vaddr);
			return -EFAULT;
		}
	} else {
		memcpy(buf, vaddr + offset, csize);
	}

	memunmap(vaddr);

	return csize;
}

/**
 * elfcorehdr_read - read from ELF core header
 * @buf: buffer where the data is placed
 * @csize: number of bytes to read
 * @ppos: address in the memory
 *
 * This function reads @count bytes from elf core header which exists
 * on crash dump kernel's memory.
 */
ssize_t elfcorehdr_read(char *buf, size_t count, u64 *ppos)
{
	memcpy(buf, phys_to_virt((phys_addr_t)*ppos), count);
	return count;
}
