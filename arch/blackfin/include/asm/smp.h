/*
 * File:         arch/blackfin/include/asm/smp.h
 * Author:       Philippe Gerum <rpm@xenomai.org>
 *
 *               Copyright 2007 Analog Devices Inc.
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __ASM_BLACKFIN_SMP_H
#define __ASM_BLACKFIN_SMP_H

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/cache.h>
#include <asm/blackfin.h>
#include <mach/smp.h>

#define raw_smp_processor_id()  blackfin_core_id()

extern char coreb_trampoline_start, coreb_trampoline_end;

struct corelock_slot {
	int lock;
};

void smp_icache_flush_range_others(unsigned long start,
				   unsigned long end);

#endif /* !__ASM_BLACKFIN_SMP_H */
