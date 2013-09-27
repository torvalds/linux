/*
 * Copyright (C) 2013 Richard Weinberger <richrd@nod.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <os.h>

long probe_kernel_read(void *dst, const void *src, size_t size)
{
	void *psrc = (void *)rounddown((unsigned long)src, PAGE_SIZE);

	if ((unsigned long)src < PAGE_SIZE || size <= 0)
		return -EFAULT;

	if (os_mincore(psrc, size + src - psrc) <= 0)
		return -EFAULT;

	return __probe_kernel_read(dst, src, size);
}
