/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2002 MIPS Technologies, Inc.  All rights reserved.
 * Copyright (C) 2003 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2004  Maciej W. Rozycki
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
 * Routines for generic manipulation of the interrupts found on the MIPS
 * Sead board.
 */
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/irq_cpu.h>
#include <asm/system.h>

#include <asm/mips-boards/seadint.h>

extern asmlinkage void mipsIRQ(void);

void __init arch_init_irq(void)
{
	mips_cpu_irq_init(MIPSCPU_INT_BASE);

	/* Now safe to set the exception vector. */
	set_except_vector(0, mipsIRQ);
}
