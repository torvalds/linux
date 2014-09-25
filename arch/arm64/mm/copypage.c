/*
 * Based on arch/arm/mm/copypage.c
 *
 * Copyright (C) 2002 Deep Blue Solutions Ltd, All Rights Reserved.
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/mm.h>

#include <asm/page.h>
#include <asm/cacheflush.h>

void __cpu_copy_user_page(void *kto, const void *kfrom, unsigned long vaddr)
{
	copy_page(kto, kfrom);
	__flush_dcache_area(kto, PAGE_SIZE);
}
EXPORT_SYMBOL_GPL(__cpu_copy_user_page);

void __cpu_clear_user_page(void *kaddr, unsigned long vaddr)
{
	clear_page(kaddr);
}
EXPORT_SYMBOL_GPL(__cpu_clear_user_page);
