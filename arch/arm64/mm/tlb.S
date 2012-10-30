/*
 * Based on arch/arm/mm/tlb.S
 *
 * Copyright (C) 1997-2002 Russell King
 * Copyright (C) 2012 ARM Ltd.
 * Written by Catalin Marinas <catalin.marinas@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/page.h>
#include <asm/tlbflush.h>
#include "proc-macros.S"

/*
 *	__cpu_flush_user_tlb_range(start, end, vma)
 *
 *	Invalidate a range of TLB entries in the specified address space.
 *
 *	- start - start address (may not be aligned)
 *	- end   - end address (exclusive, may not be aligned)
 *	- vma   - vma_struct describing address range
 */
ENTRY(__cpu_flush_user_tlb_range)
	vma_vm_mm x3, x2			// get vma->vm_mm
	mmid	x3, x3				// get vm_mm->context.id
	dsb	sy
	lsr	x0, x0, #12			// align address
	lsr	x1, x1, #12
	bfi	x0, x3, #48, #16		// start VA and ASID
	bfi	x1, x3, #48, #16		// end VA and ASID
1:	tlbi	vae1is, x0			// TLB invalidate by address and ASID
	add	x0, x0, #1
	cmp	x0, x1
	b.lo	1b
	dsb	sy
	ret
ENDPROC(__cpu_flush_user_tlb_range)

/*
 *	__cpu_flush_kern_tlb_range(start,end)
 *
 *	Invalidate a range of kernel TLB entries.
 *
 *	- start - start address (may not be aligned)
 *	- end   - end address (exclusive, may not be aligned)
 */
ENTRY(__cpu_flush_kern_tlb_range)
	dsb	sy
	lsr	x0, x0, #12			// align address
	lsr	x1, x1, #12
1:	tlbi	vaae1is, x0			// TLB invalidate by address
	add	x0, x0, #1
	cmp	x0, x1
	b.lo	1b
	dsb	sy
	isb
	ret
ENDPROC(__cpu_flush_kern_tlb_range)
