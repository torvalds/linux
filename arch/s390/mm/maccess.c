/*
 * Access kernel memory without faulting -- s390 specific implementation.
 *
 * Copyright IBM Corp. 2009
 *
 *   Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>,
 *
 */

#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <asm/system.h>

/*
 * This function writes to kernel memory bypassing DAT and possible
 * write protection. It copies one to four bytes from src to dst
 * using the stura instruction.
 * Returns the number of bytes copied or -EFAULT.
 */
static long probe_kernel_write_odd(void *dst, void *src, size_t size)
{
	unsigned long count, aligned;
	int offset, mask;
	int rc = -EFAULT;

	aligned = (unsigned long) dst & ~3UL;
	offset = (unsigned long) dst & 3;
	count = min_t(unsigned long, 4 - offset, size);
	mask = (0xf << (4 - count)) & 0xf;
	mask >>= offset;
	asm volatile(
		"	bras	1,0f\n"
		"	icm	0,0,0(%3)\n"
		"0:	l	0,0(%1)\n"
		"	lra	%1,0(%1)\n"
		"1:	ex	%2,0(1)\n"
		"2:	stura	0,%1\n"
		"	la	%0,0\n"
		"3:\n"
		EX_TABLE(0b,3b) EX_TABLE(1b,3b) EX_TABLE(2b,3b)
		: "+d" (rc), "+a" (aligned)
		: "a" (mask), "a" (src) : "cc", "memory", "0", "1");
	return rc ? rc : count;
}

long probe_kernel_write(void *dst, void *src, size_t size)
{
	long copied = 0;

	while (size) {
		copied = probe_kernel_write_odd(dst, src, size);
		if (copied < 0)
			break;
		dst += copied;
		src += copied;
		size -= copied;
	}
	return copied < 0 ? -EFAULT : 0;
}
