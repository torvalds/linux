/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 * Portions copyright (C) 2009 Cisco Systems, Inc.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Setting up the clock on the MIPS boards.
 */

#include <linux/init.h>
#include <asm/mach-powertv/interrupts.h>
#include <asm/time.h>

#include "powertv-clock.h"

unsigned int get_c0_compare_int(void)
{
	return irq_mips_timer;
}

void __init plat_time_init(void)
{
	powertv_clocksource_init();
}
