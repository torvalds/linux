/*
 * arch/arm/mach-meson8/cache.c
 *
 * Copyright (C) 2013 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <plat/io.h>
#include <mach/io.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <asm/hardware/cache-l2x0.h>

static int __init meson_cache_of_init(void)
{
	int aux = 0;
	/*
		put some default aux setting here
	*/
	
	l2x0_of_init(aux,~0);
	return 0;
}
early_initcall(meson_cache_of_init);
