// SPDX-License-Identifier: GPL-2.0
/*
 * Access kernel memory without faulting -- s390 specific implementation.
 *
 * Copyright IBM Corp. 2009, 2015
 *
 *   Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>,
 *
 */

#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/cpu.h>
#include <asm/ctl_reg.h>
#include <asm/io.h>
#include <asm/stacktrace.h>

static notrace long s390_kernel_write_odd(void *dst, const void *src, size_t size)
{
	unsigned long aligned, offset, count;
	char tmp[8];

	aligned = (unsigned long) dst & ~7UL;
	offset = (unsigned long) dst & 7UL;
	size = min(8UL - offset, size);
	count = size - 1;
	asm volatile(
		"	bras	1,0f\n"
		"	mvc	0(1,%4),0(%5)\n"
		"0:	mvc	0(8,%3),0(%0)\n"
		"	ex	%1,0(1)\n"
		"	lg	%1,0(%3)\n"
		"	lra	%0,0(%0)\n"
		"	sturg	%1,%0\n"
		: "+&a" (aligned), "+&a" (count), "=m" (tmp)
		: "a" (&tmp), "a" (&tmp[offset]), "a" (src)
		: "cc", "memory", "1");
	return size;
}

/*
 * s390_kernel_write - write to kernel memory bypassing DAT
 * @dst: destination address
 * @src: source address
 * @size: number of bytes to copy
 *
 * This function writes to kernel memory bypassing DAT and possible page table
 * write protection. It writes to the destination using the sturg instruction.
 * Therefore we have a read-modify-write sequence: the function reads eight
 * bytes from destination at an eight byte boundary, modifies the bytes
 * requested and writes the result back in a loop.
 */
static DEFINE_SPINLOCK(s390_kernel_write_lock);

notrace void *s390_kernel_write(void *dst, const void *src, size_t size)
{
	void *tmp = dst;
	unsigned long flags;
	long copied;

	spin_lock_irqsave(&s390_kernel_write_lock, flags);
	if (!(flags & PSW_MASK_DAT)) {
		memcpy(dst, src, size);
	} else {
		while (size) {
			copied = s390_kernel_write_odd(tmp, src, size);
			tmp += copied;
			src += copied;
			size -= copied;
		}
	}
	spin_unlock_irqrestore(&s390_kernel_write_lock, flags);

	return dst;
}

static int __no_sanitize_address __memcpy_real(void *dest, void *src, size_t count)
{
	union register_pair _dst, _src;
	int rc = -EFAULT;

	_dst.even = (unsigned long) dest;
	_dst.odd  = (unsigned long) count;
	_src.even = (unsigned long) src;
	_src.odd  = (unsigned long) count;
	asm volatile (
		"0:	mvcle	%[dst],%[src],0\n"
		"1:	jo	0b\n"
		"	lhi	%[rc],0\n"
		"2:\n"
		EX_TABLE(1b,2b)
		: [rc] "+&d" (rc), [dst] "+&d" (_dst.pair), [src] "+&d" (_src.pair)
		: : "cc", "memory");
	return rc;
}

static unsigned long __no_sanitize_address _memcpy_real(unsigned long dest,
							unsigned long src,
							unsigned long count)
{
	int irqs_disabled, rc;
	unsigned long flags;

	if (!count)
		return 0;
	flags = arch_local_irq_save();
	irqs_disabled = arch_irqs_disabled_flags(flags);
	if (!irqs_disabled)
		trace_hardirqs_off();
	__arch_local_irq_stnsm(0xf8); // disable DAT
	rc = __memcpy_real((void *) dest, (void *) src, (size_t) count);
	if (flags & PSW_MASK_DAT)
		__arch_local_irq_stosm(0x04); // enable DAT
	if (!irqs_disabled)
		trace_hardirqs_on();
	__arch_local_irq_ssm(flags);
	return rc;
}

/*
 * Copy memory in real mode (kernel to kernel)
 */
int memcpy_real(void *dest, void *src, size_t count)
{
	unsigned long _dest  = (unsigned long)dest;
	unsigned long _src   = (unsigned long)src;
	unsigned long _count = (unsigned long)count;
	int rc;

	if (S390_lowcore.nodat_stack != 0) {
		preempt_disable();
		rc = call_on_stack(3, S390_lowcore.nodat_stack,
				   unsigned long, _memcpy_real,
				   unsigned long, _dest,
				   unsigned long, _src,
				   unsigned long, _count);
		preempt_enable();
		return rc;
	}
	/*
	 * This is a really early memcpy_real call, the stacks are
	 * not set up yet. Just call _memcpy_real on the early boot
	 * stack
	 */
	return _memcpy_real(_dest, _src, _count);
}

/*
 * Copy memory in absolute mode (kernel to kernel)
 */
void memcpy_absolute(void *dest, void *src, size_t count)
{
	unsigned long cr0, flags, prefix;

	flags = arch_local_irq_save();
	__ctl_store(cr0, 0, 0);
	__ctl_clear_bit(0, 28); /* disable lowcore protection */
	prefix = store_prefix();
	if (prefix) {
		local_mcck_disable();
		set_prefix(0);
		memcpy(dest, src, count);
		set_prefix(prefix);
		local_mcck_enable();
	} else {
		memcpy(dest, src, count);
	}
	__ctl_load(cr0, 0, 0);
	arch_local_irq_restore(flags);
}

/*
 * Copy memory from kernel (real) to user (virtual)
 */
int copy_to_user_real(void __user *dest, void *src, unsigned long count)
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
 * Check if physical address is within prefix or zero page
 */
static int is_swapped(unsigned long addr)
{
	unsigned long lc;
	int cpu;

	if (addr < sizeof(struct lowcore))
		return 1;
	for_each_online_cpu(cpu) {
		lc = (unsigned long) lowcore_ptr[cpu];
		if (addr > lc + sizeof(struct lowcore) - 1 || addr < lc)
			continue;
		return 1;
	}
	return 0;
}

/*
 * Convert a physical pointer for /dev/mem access
 *
 * For swapped prefix pages a new buffer is returned that contains a copy of
 * the absolute memory. The buffer size is maximum one page large.
 */
void *xlate_dev_mem_ptr(phys_addr_t addr)
{
	void *bounce = (void *) addr;
	unsigned long size;

	cpus_read_lock();
	preempt_disable();
	if (is_swapped(addr)) {
		size = PAGE_SIZE - (addr & ~PAGE_MASK);
		bounce = (void *) __get_free_page(GFP_ATOMIC);
		if (bounce)
			memcpy_absolute(bounce, (void *) addr, size);
	}
	preempt_enable();
	cpus_read_unlock();
	return bounce;
}

/*
 * Free converted buffer for /dev/mem access (if necessary)
 */
void unxlate_dev_mem_ptr(phys_addr_t addr, void *buf)
{
	if ((void *) addr != buf)
		free_page((unsigned long) buf);
}
