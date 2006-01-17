/*
 * crt0_r.S: Entry function for SPU-side context restore.
 *
 * Copyright (C) 2005 IBM
 *
 * Entry and exit function for SPU-side of the context restore
 * sequence.  Sets up an initial stack frame, then branches to
 * 'main'.  On return, restores all 128 registers from the LSCSA
 * and exits.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <asm/spu_csa.h>

.data
.align 7
.globl regs_spill
regs_spill:
.space SIZEOF_SPU_SPILL_REGS, 0x0

.text
.global _start
_start:
	/* Initialize the stack pointer to point to 16368
	 * (16kb-16). The back chain pointer is initialized
	 * to NULL.
	 */
	il      $0, 0
	il      $SP, 16368
	stqd    $0, 0($SP)

	/* Allocate a minimum stack frame for the called main.
	 * This is needed so that main has a place to save the
	 * link register when it calls another function.
	 */
	stqd    $SP, -160($SP)
	ai      $SP, $SP, -160

	/* Call the program's main function. */
	brsl    $0, main

.global exit
.global	_exit
exit:
_exit:
	/* SPU Context Restore, Step 5: Restore the remaining 112 GPRs. */
	ila     $3, regs_spill + 256
restore_regs:
	lqr     $4, restore_reg_insts
restore_reg_loop:
	ai      $4, $4, 4
	.balignl 16, 0x40200000
restore_reg_insts:       /* must be quad-word aligned. */
	lqd     $16, 0($3)
	lqd     $17, 16($3)
	lqd     $18, 32($3)
	lqd     $19, 48($3)
	andi    $5, $4, 0x7F
	stqr    $4, restore_reg_insts
	ai      $3, $3, 64
	brnz    $5, restore_reg_loop

	/* SPU Context Restore Step 17: Restore the first 16 GPRs. */
	lqa $0, regs_spill + 0
	lqa $1, regs_spill + 16
	lqa $2, regs_spill + 32
	lqa $3, regs_spill + 48
	lqa $4, regs_spill + 64
	lqa $5, regs_spill + 80
	lqa $6, regs_spill + 96
	lqa $7, regs_spill + 112
	lqa $8, regs_spill + 128
	lqa $9, regs_spill + 144
	lqa $10, regs_spill + 160
	lqa $11, regs_spill + 176
	lqa $12, regs_spill + 192
	lqa $13, regs_spill + 208
	lqa $14, regs_spill + 224
	lqa $15, regs_spill + 240

	/* Under normal circumstances, the 'exit' function
	 * terminates with 'stop SPU_RESTORE_COMPLETE',
	 * indicating that the SPU-side restore code has
	 * completed.
	 *
	 * However it is possible that instructions immediately
	 * following the 'stop 0x3ffc' have been modified at run
	 * time so as to recreate the exact SPU_Status settings
	 * from the application, e.g. illegal instruciton, halt,
	 * etc.
	 */
.global exit_fini
.global	_exit_fini
exit_fini:
_exit_fini:
	stop	SPU_RESTORE_COMPLETE
	stop	0
	stop	0
	stop	0

	/* Pad the size of this crt0.o to be multiple of 16 bytes. */
.balignl 16, 0x0
