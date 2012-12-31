/* linux/arch/arm/mach-exynos/include/mach/clock-domain.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS - Clock Domain support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_CLOCK_DOMAIN_H
#define __ASM_ARCH_CLOCK_DOMAIN_H __FILE__

#define LPA_DOMAIN	0x00000001

struct clock {
	struct list_head node;

	struct clk *clk;
};

struct clock_domain {
	struct list_head node;

	unsigned int flag;
	struct list_head domain_list;
};

int clock_add_domain(unsigned int flag, struct clk *clk);
int clock_domain_enabled(unsigned int flag);
#endif /* __ASM_ARCH_CLOCK_DOMAIN_H */
