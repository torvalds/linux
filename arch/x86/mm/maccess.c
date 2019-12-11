// SPDX-License-Identifier: GPL-2.0-only

#include <linux/uaccess.h>
#include <linux/kernel.h>

#ifdef CONFIG_X86_64
static __always_inline u64 canonical_address(u64 vaddr, u8 vaddr_bits)
{
	return ((s64)vaddr << (64 - vaddr_bits)) >> (64 - vaddr_bits);
}

static __always_inline bool invalid_probe_range(u64 vaddr)
{
	/*
	 * Range covering the highest possible canonical userspace address
	 * as well as non-canonical address range. For the canonical range
	 * we also need to include the userspace guard page.
	 */
	return vaddr < TASK_SIZE_MAX + PAGE_SIZE ||
	       canonical_address(vaddr, boot_cpu_data.x86_virt_bits) != vaddr;
}
#else
static __always_inline bool invalid_probe_range(u64 vaddr)
{
	return vaddr < TASK_SIZE_MAX;
}
#endif

long probe_kernel_read_strict(void *dst, const void *src, size_t size)
{
	if (unlikely(invalid_probe_range((unsigned long)src)))
		return -EFAULT;

	return __probe_kernel_read(dst, src, size);
}

long strncpy_from_unsafe_strict(char *dst, const void *unsafe_addr, long count)
{
	if (unlikely(invalid_probe_range((unsigned long)unsafe_addr)))
		return -EFAULT;

	return __strncpy_from_unsafe(dst, unsafe_addr, count);
}
