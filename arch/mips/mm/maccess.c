// SPDX-License-Identifier: GPL-2.0-only

#include <linux/uaccess.h>
#include <linux/kernel.h>

bool copy_from_kernel_nofault_allowed(const void *unsafe_src, size_t size)
{
	/* highest bit set means kernel space */
	return (unsigned long)unsafe_src >> (BITS_PER_LONG - 1);
}
