/*
 *  Kevin D. Kissell, kevink@mips and Carsten Langgaard, carstenl@mips.com
 *  Copyright (C) 2000 MIPS Technologies, Inc.	All rights reserved.
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
 * Routines corresponding to Linux kernel FP context
 * manipulation primitives for the Algorithmics MIPS
 * FPU Emulator
 */
#include <linux/sched.h>
#include <asm/processor.h>
#include <asm/signal.h>
#include <asm/uaccess.h>

#include <asm/fpu.h>
#include <asm/fpu_emulator.h>

#define SIGNALLING_NAN 0x7ff800007ff80000LL

void fpu_emulator_init_fpu(void)
{
	static int first = 1;
	int i;

	if (first) {
		first = 0;
		printk("Algorithmics/MIPS FPU Emulator v1.5\n");
	}

	current->thread.fpu.fcr31 = 0;
	for (i = 0; i < 32; i++)
		set_fpr64(&current->thread.fpu.fpr[i], 0, SIGNALLING_NAN);
}
