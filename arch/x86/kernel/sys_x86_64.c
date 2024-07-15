// SPDX-License-Identifier: GPL-2.0
#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/syscalls.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/personality.h>
#include <linux/random.h>
#include <linux/uaccess.h>
#include <linux/elf.h>

#include <asm/elf.h>
#include <asm/ia32.h>

/*
 * Align a virtual address to avoid aliasing in the I$ on AMD F15h.
 */
static unsigned long get_align_mask(void)
{
	/* handle 32- and 64-bit case with a single conditional */
	if (va_align.flags < 0 || !(va_align.flags & (2 - mmap_is_ia32())))
		return 0;

	if (!(current->flags & PF_RANDOMIZE))
		return 0;

	return va_align.mask;
}

/*
 * To avoid aliasing in the I$ on AMD F15h, the bits defined by the
 * va_align.bits, [12:upper_bit), are set to a random value instead of
 * zeroing them. This random value is computed once per boot. This form
 * of ASLR is known as "per-boot ASLR".
 *
 * To achieve this, the random value is added to the info.align_offset
 * value before calling vm_unmapped_area() or ORed directly to the
 * address.
 */
static unsigned long get_align_bits(void)
{
	return va_align.bits & get_align_mask();
}

static int __init control_va_addr_alignment(char *str)
{
	/* guard against enabling this on other CPU families */
	if (va_align.flags < 0)
		return 1;

	if (*str == 0)
		return 1;

	if (!strcmp(str, "32"))
		va_align.flags = ALIGN_VA_32;
	else if (!strcmp(str, "64"))
		va_align.flags = ALIGN_VA_64;
	else if (!strcmp(str, "off"))
		va_align.flags = 0;
	else if (!strcmp(str, "on"))
		va_align.flags = ALIGN_VA_32 | ALIGN_VA_64;
	else
		pr_warn("invalid option value: 'align_va_addr=%s'\n", str);

	return 1;
}
__setup("align_va_addr=", control_va_addr_alignment);

SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, off)
{
	if (off & ~PAGE_MASK)
		return -EINVAL;

	return ksys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
}

static void find_start_end(unsigned long addr, unsigned long flags,
		unsigned long *begin, unsigned long *end)
{
	if (!in_32bit_syscall() && (flags & MAP_32BIT)) {
		/* This is usually used needed to map code in small
		   model, so it needs to be in the first 31bit. Limit
		   it to that.  This means we need to move the
		   unmapped base down for this case. This can give
		   conflicts with the heap, but we assume that glibc
		   malloc knows how to fall back to mmap. Give it 1GB
		   of playground for now. -AK */
		*begin = 0x40000000;
		*end = 0x80000000;
		if (current->flags & PF_RANDOMIZE) {
			*begin = randomize_page(*begin, 0x02000000);
		}
		return;
	}

	*begin	= get_mmap_base(1);
	if (in_32bit_syscall())
		*end = task_size_32bit();
	else
		*end = task_size_64bit(addr > DEFAULT_MAP_WINDOW);
}

static inline unsigned long stack_guard_placement(vm_flags_t vm_flags)
{
	if (vm_flags & VM_SHADOW_STACK)
		return PAGE_SIZE;

	return 0;
}

unsigned long
arch_get_unmapped_area_vmflags(struct file *filp, unsigned long addr, unsigned long len,
		       unsigned long pgoff, unsigned long flags, vm_flags_t vm_flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct vm_unmapped_area_info info = {};
	unsigned long begin, end;

	if (flags & MAP_FIXED)
		return addr;

	find_start_end(addr, flags, &begin, &end);

	if (len > end)
		return -ENOMEM;

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(mm, addr);
		if (end - len >= addr &&
		    (!vma || addr + len <= vm_start_gap(vma)))
			return addr;
	}

	info.length = len;
	info.low_limit = begin;
	info.high_limit = end;
	info.align_offset = pgoff << PAGE_SHIFT;
	info.start_gap = stack_guard_placement(vm_flags);
	if (filp) {
		info.align_mask = get_align_mask();
		info.align_offset += get_align_bits();
	}
	return vm_unmapped_area(&info);
}

unsigned long
arch_get_unmapped_area_topdown_vmflags(struct file *filp, unsigned long addr0,
			  unsigned long len, unsigned long pgoff,
			  unsigned long flags, vm_flags_t vm_flags)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	unsigned long addr = addr0;
	struct vm_unmapped_area_info info = {};

	/* requested length too big for entire address space */
	if (len > TASK_SIZE)
		return -ENOMEM;

	/* No address checking. See comment at mmap_address_hint_valid() */
	if (flags & MAP_FIXED)
		return addr;

	/* for MAP_32BIT mappings we force the legacy mmap base */
	if (!in_32bit_syscall() && (flags & MAP_32BIT))
		goto bottomup;

	/* requesting a specific address */
	if (addr) {
		addr &= PAGE_MASK;
		if (!mmap_address_hint_valid(addr, len))
			goto get_unmapped_area;

		vma = find_vma(mm, addr);
		if (!vma || addr + len <= vm_start_gap(vma))
			return addr;
	}
get_unmapped_area:

	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
	info.length = len;
	if (!in_32bit_syscall() && (flags & MAP_ABOVE4G))
		info.low_limit = SZ_4G;
	else
		info.low_limit = PAGE_SIZE;

	info.high_limit = get_mmap_base(0);
	info.start_gap = stack_guard_placement(vm_flags);

	/*
	 * If hint address is above DEFAULT_MAP_WINDOW, look for unmapped area
	 * in the full address space.
	 *
	 * !in_32bit_syscall() check to avoid high addresses for x32
	 * (and make it no op on native i386).
	 */
	if (addr > DEFAULT_MAP_WINDOW && !in_32bit_syscall())
		info.high_limit += TASK_SIZE_MAX - DEFAULT_MAP_WINDOW;

	info.align_offset = pgoff << PAGE_SHIFT;
	if (filp) {
		info.align_mask = get_align_mask();
		info.align_offset += get_align_bits();
	}
	addr = vm_unmapped_area(&info);
	if (!(addr & ~PAGE_MASK))
		return addr;
	VM_BUG_ON(addr != -ENOMEM);

bottomup:
	/*
	 * A failed mmap() very likely causes application failure,
	 * so fall back to the bottom-up function here. This scenario
	 * can happen with large stack limits and large mmap()
	 * allocations.
	 */
	return arch_get_unmapped_area(filp, addr0, len, pgoff, flags);
}

unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	return arch_get_unmapped_area_vmflags(filp, addr, len, pgoff, flags, 0);
}

unsigned long
arch_get_unmapped_area_topdown(struct file *filp, const unsigned long addr,
			  const unsigned long len, const unsigned long pgoff,
			  const unsigned long flags)
{
	return arch_get_unmapped_area_topdown_vmflags(filp, addr, len, pgoff, flags, 0);
}
