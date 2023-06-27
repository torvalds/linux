/*
 * arch/xtensa/kernel/platform.c
 *
 * Default platform functions.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 */

#include <linux/printk.h>
#include <linux/types.h>
#include <asm/platform.h>
#include <asm/timex.h>

/*
 * Default functions that are used if no platform specific function is defined.
 * (Please, refer to arch/xtensa/include/asm/platform.h for more information)
 */

void __weak __init platform_init(bp_tag_t *first)
{
}

void __weak __init platform_setup(char **cmd)
{
}

void __weak platform_idle(void)
{
	__asm__ __volatile__ ("waiti 0" ::: "memory");
}

#ifdef CONFIG_XTENSA_CALIBRATE_CCOUNT
void __weak platform_calibrate_ccount(void)
{
	pr_err("ERROR: Cannot calibrate cpu frequency! Assuming 10MHz.\n");
	ccount_freq = 10 * 1000000UL;
}
#endif
