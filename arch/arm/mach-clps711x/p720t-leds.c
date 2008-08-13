/*
 *  linux/arch/arm/mach-clps711x/leds.c
 *
 *  Integrator LED control routines
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/leds.h>
#include <asm/system.h>
#include <asm/mach-types.h>

#include <asm/hardware/clps7111.h>
#include <asm/hardware/ep7212.h>

static void p720t_leds_event(led_event_t ledevt)
{
	unsigned long flags;
	u32 pddr;

	local_irq_save(flags);
	switch(ledevt) {
	case led_idle_start:
		break;

	case led_idle_end:
		break;

	case led_timer:
		pddr = clps_readb(PDDR);
		clps_writeb(pddr ^ 1, PDDR);
		break;

	default:
		break;
	}

	local_irq_restore(flags);
}

static int __init leds_init(void)
{
	if (machine_is_p720t())
		leds_event = p720t_leds_event;

	return 0;
}

arch_initcall(leds_init);
