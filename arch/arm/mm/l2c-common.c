/*
 * Copyright (C) 2010 ARM Ltd.
 * Written by Catalin Marinas <catalin.marinas@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/bug.h>
#include <linux/smp.h>
#include <asm/outercache.h>

void outer_disable(void)
{
	WARN_ON(!irqs_disabled());
	WARN_ON(num_online_cpus() > 1);

	if (outer_cache.disable)
		outer_cache.disable();
}
