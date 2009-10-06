/*
 * File:         include/asm-blackfin/early_printk.h
 * Author:       Robin Getz <rgetz@blackfin.uclinux.org
 *
 * Created:      14Aug2007
 * Description:  function prototpyes for early printk
 *
 * Modified:
 *               Copyright 2004-2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#ifndef __ASM_EARLY_PRINTK_H__
#define __ASM_EARLY_PRINTK_H__

#ifdef CONFIG_EARLY_PRINTK
/* For those that don't include it already */
#include <linux/console.h>

extern int setup_early_printk(char *);
extern void enable_shadow_console(void);
extern int shadow_console_enabled(void);
extern void mark_shadow_error(void);
extern void early_shadow_reg(unsigned long reg, unsigned int n);
extern void early_shadow_write(struct console *con, const char *s,
	unsigned int n) __attribute__((nonnull(2)));
#define early_shadow_puts(str) early_shadow_write(NULL, str, strlen(str))
#define early_shadow_stamp() \
	do { \
		early_shadow_puts(__FILE__ " : " __stringify(__LINE__) " ["); \
		early_shadow_puts(__func__); \
		early_shadow_puts("]\n"); \
	} while (0)
#else
#define setup_early_printk(fmt) do { } while (0)
#define enable_shadow_console(fmt)  do { } while (0)
#define early_shadow_stamp() do { } while (0)
#endif /* CONFIG_EARLY_PRINTK */

#endif /* __ASM_EARLY_PRINTK_H__ */
