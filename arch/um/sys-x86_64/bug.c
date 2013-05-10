/*
 * Copyright (C) 2006 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL V2
 */

#include <linux/uaccess.h>

/*
 * Mostly copied from i386/x86_86 - eliminated the eip < PAGE_OFFSET because
 * that's not relevant in skas mode.
 */

int is_valid_bugaddr(unsigned long eip)
{
	unsigned short ud2;

	if (probe_kernel_address((unsigned short __user *)eip, ud2))
		return 0;

	return ud2 == 0x0b0f;
}
