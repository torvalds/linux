/* Handle the cache being disabled
 *
 * Copyright (C) 2010 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/mm.h>

/*
 * allow userspace to flush the instruction cache
 */
asmlinkage long sys_cacheflush(unsigned long start, unsigned long end)
{
	if (end < start)
		return -EINVAL;
	return 0;
}
