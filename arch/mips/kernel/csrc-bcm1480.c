/*
 * Copyright (C) 2000,2001,2004 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/clocksource.h>

#include <asm/addrspace.h>
#include <asm/io.h>
#include <asm/time.h>

#include <asm/sibyte/bcm1480_regs.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/bcm1480_int.h>
#include <asm/sibyte/bcm1480_scd.h>

#include <asm/sibyte/sb1250.h>

static cycle_t bcm1480_hpt_read(struct clocksource *cs)
{
	return (cycle_t) __raw_readq(IOADDR(A_SCD_ZBBUS_CYCLE_COUNT));
}

struct clocksource bcm1480_clocksource = {
	.name	= "zbbus-cycles",
	.rating	= 200,
	.read	= bcm1480_hpt_read,
	.mask	= CLOCKSOURCE_MASK(64),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

void __init sb1480_clocksource_init(void)
{
	struct clocksource *cs = &bcm1480_clocksource;
	unsigned int plldiv;
	unsigned long zbbus;

	plldiv = G_BCM1480_SYS_PLL_DIV(__raw_readq(IOADDR(A_SCD_SYSTEM_CFG)));
	zbbus = ((plldiv >> 1) * 50000000) + ((plldiv & 1) * 25000000);
	clocksource_register_hz(cs, zbbus);
}
