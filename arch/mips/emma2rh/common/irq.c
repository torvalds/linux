/*
 *  arch/mips/emma2rh/common/irq.c
 *      This file is common irq dispatcher.
 *
 *  Copyright (C) NEC Electronics Corporation 2005-2006
 *
 *  This file is based on the arch/mips/ddb5xxx/ddb5477/irq.c
 *
 *	Copyright 2001 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/types.h>

#include <asm/system.h>
#include <asm/mipsregs.h>
#include <asm/debug.h>
#include <asm/addrspace.h>
#include <asm/bootinfo.h>

#include <asm/emma2rh/emma2rh.h>

/*
 * the first level int-handler will jump here if it is a emma2rh irq
 */
void emma2rh_irq_dispatch(void)
{
	u32 intStatus;
	u32 bitmask;
	u32 i;

	intStatus = emma2rh_in32(EMMA2RH_BHIF_INT_ST_0)
	    & emma2rh_in32(EMMA2RH_BHIF_INT_EN_0);

#ifdef EMMA2RH_SW_CASCADE
	if (intStatus &
	    (1 << ((EMMA2RH_SW_CASCADE - EMMA2RH_IRQ_INT0) & (32 - 1)))) {
		u32 swIntStatus;
		swIntStatus = emma2rh_in32(EMMA2RH_BHIF_SW_INT)
		    & emma2rh_in32(EMMA2RH_BHIF_SW_INT_EN);
		for (i = 0, bitmask = 1; i < 32; i++, bitmask <<= 1) {
			if (swIntStatus & bitmask) {
				do_IRQ(EMMA2RH_SW_IRQ_BASE + i);
				return;
			}
		}
	}
#endif

	for (i = 0, bitmask = 1; i < 32; i++, bitmask <<= 1) {
		if (intStatus & bitmask) {
			do_IRQ(EMMA2RH_IRQ_BASE + i);
			return;
		}
	}

	intStatus = emma2rh_in32(EMMA2RH_BHIF_INT_ST_1)
	    & emma2rh_in32(EMMA2RH_BHIF_INT_EN_1);

#ifdef EMMA2RH_GPIO_CASCADE
	if (intStatus &
	    (1 << ((EMMA2RH_GPIO_CASCADE - EMMA2RH_IRQ_INT0) & (32 - 1)))) {
		u32 gpioIntStatus;
		gpioIntStatus = emma2rh_in32(EMMA2RH_GPIO_INT_ST)
		    & emma2rh_in32(EMMA2RH_GPIO_INT_MASK);
		for (i = 0, bitmask = 1; i < 32; i++, bitmask <<= 1) {
			if (gpioIntStatus & bitmask) {
				do_IRQ(EMMA2RH_GPIO_IRQ_BASE + i);
				return;
			}
		}
	}
#endif

	for (i = 32, bitmask = 1; i < 64; i++, bitmask <<= 1) {
		if (intStatus & bitmask) {
			do_IRQ(EMMA2RH_IRQ_BASE + i);
			return;
		}
	}

	intStatus = emma2rh_in32(EMMA2RH_BHIF_INT_ST_2)
	    & emma2rh_in32(EMMA2RH_BHIF_INT_EN_2);

	for (i = 64, bitmask = 1; i < 96; i++, bitmask <<= 1) {
		if (intStatus & bitmask) {
			do_IRQ(EMMA2RH_IRQ_BASE + i);
			return;
		}
	}
}
