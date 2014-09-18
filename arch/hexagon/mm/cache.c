/*
 * Cache management functions for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/mm.h>
#include <asm/cacheflush.h>
#include <asm/hexagon_vm.h>

#define spanlines(start, end) \
	(((end - (start & ~(LINESIZE - 1))) >> LINEBITS) + 1)

void flush_dcache_range(unsigned long start, unsigned long end)
{
	unsigned long lines = spanlines(start, end-1);
	unsigned long i, flags;

	start &= ~(LINESIZE - 1);

	local_irq_save(flags);

	for (i = 0; i < lines; i++) {
		__asm__ __volatile__ (
		"	dccleaninva(%0);	"
		:
		: "r" (start)
		);
		start += LINESIZE;
	}
	local_irq_restore(flags);
}

void flush_icache_range(unsigned long start, unsigned long end)
{
	unsigned long lines = spanlines(start, end-1);
	unsigned long i, flags;

	start &= ~(LINESIZE - 1);

	local_irq_save(flags);

	for (i = 0; i < lines; i++) {
		__asm__ __volatile__ (
			"	dccleana(%0); "
			"	icinva(%0);	"
			:
			: "r" (start)
		);
		start += LINESIZE;
	}
	__asm__ __volatile__ (
		"isync"
	);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(flush_icache_range);

void hexagon_clean_dcache_range(unsigned long start, unsigned long end)
{
	unsigned long lines = spanlines(start, end-1);
	unsigned long i, flags;

	start &= ~(LINESIZE - 1);

	local_irq_save(flags);

	for (i = 0; i < lines; i++) {
		__asm__ __volatile__ (
		"	dccleana(%0);	"
		:
		: "r" (start)
		);
		start += LINESIZE;
	}
	local_irq_restore(flags);
}

void hexagon_inv_dcache_range(unsigned long start, unsigned long end)
{
	unsigned long lines = spanlines(start, end-1);
	unsigned long i, flags;

	start &= ~(LINESIZE - 1);

	local_irq_save(flags);

	for (i = 0; i < lines; i++) {
		__asm__ __volatile__ (
		"	dcinva(%0);	"
		:
		: "r" (start)
		);
		start += LINESIZE;
	}
	local_irq_restore(flags);
}




/*
 * This is just really brutal and shouldn't be used anyways,
 * especially on V2.  Left here just in case.
 */
void flush_cache_all_hexagon(void)
{
	unsigned long flags;
	local_irq_save(flags);
	__vmcache_ickill();
	__vmcache_dckill();
	__vmcache_l2kill();
	local_irq_restore(flags);
	mb();
}
