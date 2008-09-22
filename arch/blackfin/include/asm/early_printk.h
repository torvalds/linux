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

#ifdef CONFIG_EARLY_PRINTK
extern int setup_early_printk(char *);
#else
#define setup_early_printk(fmt) do { } while (0)
#endif /* CONFIG_EARLY_PRINTK */
