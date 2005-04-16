/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#include <asm/mipsregs.h>
#include <asm/page.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

	.macro tlb_do_page_fault, write
	NESTED(tlb_do_page_fault_\write, PT_SIZE, sp)
	SAVE_ALL
	MFC0	a2, CP0_BADVADDR
	KMODE
	move	a0, sp
	REG_S	a2, PT_BVADDR(sp)
	li	a1, \write
	jal	do_page_fault
	j	ret_from_exception
	END(tlb_do_page_fault_\write)
	.endm

	tlb_do_page_fault 0
	tlb_do_page_fault 1
