// SPDX-License-Identifier: GPL-2.0
/*
 * Access kernel memory without faulting -- s390 specific implementation.
 *
 * Copyright IBM Corp. 2009, 2015
 *
 */

#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/cpu.h>
#include <asm/asm-extable.h>
#include <asm/ctl_reg.h>
#include <asm/io.h>
#include <asm/abs_lowcore.h>
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
int memcpy_real(void *dest, unsigned long src, size_t count)
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
 * Find CPU that owns swapped prefix page
 */
static int get_swapped_owner(phys_addr_t addr)
{
	phys_addr_t lc;
	int cpu;

	for_each_online_cpu(cpu) {
		lc = virt_to_phys(lowcore_ptr[cpu]);
		if (addr > lc + sizeof(struct lowcore) - 1 || addr < lc)
			continue;
		return cpu;
	}
	return -1;
}

/*
 * Convert a physical pointer for /dev/mem access
 *
 * For swapped prefix pages a new buffer is returned that contains a copy of
 * the absolute memory. The buffer size is maximum one page large.
 */
void *xlate_dev_mem_ptr(phys_addr_t addr)
{
	void *ptr = phys_to_virt(addr);
	void *bounce = ptr;
	struct lowcore *abs_lc;
	unsigned long flags;
	unsigned long size;
	int this_cpu, cpu;

	cpus_read_lock();
	this_cpu = get_cpu();
	if (addr >= sizeof(struct lowcore)) {
		cpu = get_swapped_owner(addr);
		if (cpu < 0)
			goto out;
	}
	bounce = (void *)__get_free_page(GFP_ATOMIC);
	if (!bounce)
		goto out;
	size = PAGE_SIZE - (addr & ~PAGE_MASK);
	if (addr < sizeof(struct lowcore)) {
		abs_lc = get_abs_lowcore(&flags);
		ptr = (void *)abs_lc + addr;
		memcpy(bounce, ptr, size);
		put_abs_lowcore(abs_lc, flags);
	} else if (cpu == this_cpu) {
		ptr = (void *)(addr - virt_to_phys(lowcore_ptr[cpu]));
		memcpy(bounce, ptr, size);
	} else {
		memcpy(bounce, ptr, size);
	}
out:
	put_cpu();
	cpus_read_unlock();
	return bounce;
}

/*
 * Free converted buffer for /dev/mem access (if necessary)
 */
void unxlate_dev_mem_ptr(phys_addr_t addr, void *ptr)
{
	if (addr != virt_to_phys(ptr))
		free_page((unsigned long)ptr);
}
