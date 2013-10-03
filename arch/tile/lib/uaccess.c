/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/uaccess.h>
#include <linux/module.h>

int __range_ok(unsigned long addr, unsigned long size)
{
	unsigned long limit = current_thread_info()->addr_limit.seg;
	return !((addr < limit && size <= limit - addr) ||
		 is_arch_mappable_range(addr, size));
}
EXPORT_SYMBOL(__range_ok);
