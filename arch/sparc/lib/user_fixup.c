/* user_fixup.c: Fix up user copy faults.
 *
 * Copyright (C) 2004 David S. Miller <davem@redhat.com>
 */

#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/module.h>

#include <asm/uaccess.h>

/* Calculating the exact fault address when using
 * block loads and stores can be very complicated.
 *
 * Instead of trying to be clever and handling all
 * of the cases, just fix things up simply here.
 */

static unsigned long compute_size(unsigned long start, unsigned long size, unsigned long *offset)
{
	unsigned long fault_addr = current_thread_info()->fault_address;
	unsigned long end = start + size;

	if (fault_addr < start || fault_addr >= end) {
		*offset = 0;
	} else {
		*offset = fault_addr - start;
		size = end - fault_addr;
	}
	return size;
}

unsigned long copy_from_user_fixup(void *to, const void __user *from, unsigned long size)
{
	unsigned long offset;

	size = compute_size((unsigned long) from, size, &offset);
	if (likely(size))
		memset(to + offset, 0, size);

	return size;
}
EXPORT_SYMBOL(copy_from_user_fixup);

unsigned long copy_to_user_fixup(void __user *to, const void *from, unsigned long size)
{
	unsigned long offset;

	return compute_size((unsigned long) to, size, &offset);
}
EXPORT_SYMBOL(copy_to_user_fixup);

unsigned long copy_in_user_fixup(void __user *to, void __user *from, unsigned long size)
{
	unsigned long fault_addr = current_thread_info()->fault_address;
	unsigned long start = (unsigned long) to;
	unsigned long end = start + size;

	if (fault_addr >= start && fault_addr < end)
		return end - fault_addr;

	start = (unsigned long) from;
	end = start + size;
	if (fault_addr >= start && fault_addr < end)
		return end - fault_addr;

	return size;
}
EXPORT_SYMBOL(copy_in_user_fixup);
