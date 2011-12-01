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
#include <linux/gfp.h>
#include <asm/system.h>

/*
 * This function writes to kernel memory bypassing DAT and possible
 * write protection. It copies one to four bytes from src to dst
 * using the stura instruction.
 * Returns the number of bytes copied or -EFAULT.
 */
static long probe_kernel_write_odd(void *dst, const void *src, size_t size)
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

long probe_kernel_write(void *dst, const void *src, size_t size)
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

/*
 * Copy memory in real mode (kernel to kernel)
 */
int memcpy_real(void *dest, void *src, size_t count)
{
	register unsigned long _dest asm("2") = (unsigned long) dest;
	register unsigned long _len1 asm("3") = (unsigned long) count;
	register unsigned long _src  asm("4") = (unsigned long) src;
	register unsigned long _len2 asm("5") = (unsigned long) count;
	unsigned long flags;
	int rc = -EFAULT;

	if (!count)
		return 0;
	flags = __arch_local_irq_stnsm(0xf8UL);
	asm volatile (
		"0:	mvcle	%1,%2,0x0\n"
		"1:	jo	0b\n"
		"	lhi	%0,0x0\n"
		"2:\n"
		EX_TABLE(1b,2b)
		: "+d" (rc), "+d" (_dest), "+d" (_src), "+d" (_len1),
		  "+d" (_len2), "=m" (*((long *) dest))
		: "m" (*((long *) src))
		: "cc", "memory");
	arch_local_irq_restore(flags);
	return rc;
}

/*
 * Copy memory to absolute zero
 */
void copy_to_absolute_zero(void *dest, void *src, size_t count)
{
	unsigned long cr0;

	BUG_ON((unsigned long) dest + count >= sizeof(struct _lowcore));
	preempt_disable();
	__ctl_store(cr0, 0, 0);
	__ctl_clear_bit(0, 28); /* disable lowcore protection */
	memcpy_real(dest + store_prefix(), src, count);
	__ctl_load(cr0, 0, 0);
	preempt_enable();
}

/*
 * Copy memory from kernel (real) to user (virtual)
 */
int copy_to_user_real(void __user *dest, void *src, size_t count)
{
	int offs = 0, size, rc;
	char *buf;

	buf = (char *) __get_free_page(GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	rc = -EFAULT;
	while (offs < count) {
		size = min(PAGE_SIZE, count - offs);
		if (memcpy_real(buf, src + offs, size))
			goto out;
		if (copy_to_user(dest + offs, buf, size))
			goto out;
		offs += size;
	}
	rc = 0;
out:
	free_page((unsigned long) buf);
	return rc;
}

/*
 * Copy memory from user (virtual) to kernel (real)
 */
int copy_from_user_real(void *dest, void __user *src, size_t count)
{
	int offs = 0, size, rc;
	char *buf;

	buf = (char *) __get_free_page(GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	rc = -EFAULT;
	while (offs < count) {
		size = min(PAGE_SIZE, count - offs);
		if (copy_from_user(buf, src + offs, size))
			goto out;
		if (memcpy_real(dest + offs, buf, size))
			goto out;
		offs += size;
	}
	rc = 0;
out:
	free_page((unsigned long) buf);
	return rc;
}
