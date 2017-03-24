/*
 *	kernel/crash_dump.c - Memory preserving reboot related code.
 *
 *	Created by: Simon Horman <horms@verge.net.au>
 *	Original code moved from kernel/crash.c
 *	Original code comment copied from the i386 version of this file
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/crash_dump.h>

#include <asm/page.h>
#include <linux/uaccess.h>

/**
 * copy_oldmem_page - copy one page from "oldmem"
 * @pfn: page frame number to be copied
 * @buf: target memory address for the copy; this can be in kernel address
 *	space or user address space (see @userbuf)
 * @csize: number of bytes to copy
 * @offset: offset in bytes into the page (based on pfn) to begin the copy
 * @userbuf: if set, @buf is in user address space, use copy_to_user(),
 *	otherwise @buf is in kernel address space, use memcpy().
 *
 * Copy a page from "oldmem". For this page, there is no pte mapped
 * in the current kernel. We stitch up a pte, similar to kmap_atomic.
 *
 * Calling copy_to_user() in atomic context is not desirable. Hence first
 * copying the data to a pre-allocated kernel page and then copying to user
 * space in non-atomic context.
 */
ssize_t
copy_oldmem_page(unsigned long pfn, char *buf,
		size_t csize, unsigned long offset, int userbuf)
{
	void  *vaddr;

	if (!csize)
		return 0;
	vaddr = __va(pfn<<PAGE_SHIFT);
	if (userbuf) {
		if (copy_to_user(buf, (vaddr + offset), csize)) {
			return -EFAULT;
		}
	} else
		memcpy(buf, (vaddr + offset), csize);
	return csize;
}

