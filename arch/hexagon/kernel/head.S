/*
 * Early kernel startup code for Hexagon
 *
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/asm-offsets.h>
#include <asm/mem-layout.h>
#include <asm/vm_mmu.h>
#include <asm/page.h>
#include <asm/hexagon_vm.h>

#define SEGTABLE_ENTRIES #0x0e0

	__INIT
ENTRY(stext)
	/*
	 * VMM will already have set up true vector page, MMU, etc.
	 * To set up initial kernel identity map, we have to pass
	 * the VMM a pointer to some canonical page tables. In
	 * this implementation, we're assuming that we've got
	 * them precompiled. Generate value in R24, as we'll need
	 * it again shortly.
	 */
	r24.L = #LO(swapper_pg_dir)
	r24.H = #HI(swapper_pg_dir)

	/*
	 * Symbol is kernel segment address, but we need
	 * the logical/physical address.
	 */
	r25 = pc;
	r2.h = #0xffc0;
	r2.l = #0x0000;
	r25 = and(r2,r25);	/*  R25 holds PHYS_OFFSET now  */
	r1.h = #HI(PAGE_OFFSET);
	r1.l = #LO(PAGE_OFFSET);
	r24 = sub(r24,r1);	/* swapper_pg_dir - PAGE_OFFSET */
	r24 = add(r24,r25);	/* + PHYS_OFFSET */

	r0 = r24;  /* aka __pa(swapper_pg_dir)  */

	/*
	 * Initialize page dir to make the virtual and physical
	 * addresses where the kernel was loaded be identical.
	 * Done in 4MB chunks.
	 */
#define PTE_BITS ( __HVM_PTE_R | __HVM_PTE_W | __HVM_PTE_X	\
		  | __HEXAGON_C_WB_L2 << 6			\
		  | __HVM_PDE_S_4MB)

	/*
	 * Get number of VA=PA entries; only really needed for jump
	 * to hyperspace; gets blown away immediately after
	 */

	{
		r1.l = #LO(_end);
		r2.l = #LO(stext);
		r3 = #1;
	}
	{
		r1.h = #HI(_end);
		r2.h = #HI(stext);
		r3 = asl(r3, #22);
	}
	{
		r1 = sub(r1, r2);
		r3 = add(r3, #-1);
	}  /* r1 =  _end - stext  */
	r1 = add(r1, r3);  /*  + (4M-1) */
	r26 = lsr(r1, #22); /*  / 4M = # of entries */

	r1 = r25;
	r2.h = #0xffc0;
	r2.l = #0x0000;		/* round back down to 4MB boundary  */
	r1 = and(r1,r2);
	r2 = lsr(r1, #22)	/* 4MB page number		*/
	r2 = asl(r2, #2)	/* times sizeof(PTE) (4bytes)	*/
	r0 = add(r0,r2)		/* r0 = address of correct PTE	*/
	r2 = #PTE_BITS
	r1 = add(r1,r2)		/* r1 = 4MB PTE for the first entry	*/
	r2.h = #0x0040
	r2.l = #0x0000		/* 4MB increments */
	loop0(1f,r26);
1:
	memw(r0 ++ #4) = r1
	{ r1 = add(r1, r2); } :endloop0

	/*  Also need to overwrite the initial 0xc0000000 entries  */
	/*  PAGE_OFFSET >> (4MB shift - 4 bytes per entry shift)  */
	R1.H = #HI(PAGE_OFFSET >> (22 - 2))
	R1.L = #LO(PAGE_OFFSET >> (22 - 2))

	r0 = add(r1, r24);	/* advance to 0xc0000000 entry */
	r1 = r25;
	r2.h = #0xffc0;
	r2.l = #0x0000;		/* round back down to 4MB boundary  */
	r1 = and(r1,r2);	/* for huge page */
	r2 = #PTE_BITS
	r1 = add(r1,r2);
	r2.h = #0x0040
	r2.l = #0x0000		/* 4MB increments */

	loop0(1f,SEGTABLE_ENTRIES);
1:
	memw(r0 ++ #4) = r1;
	{ r1 = add(r1,r2); } :endloop0

	r0 = r24;

	/*
	 * The subroutine wrapper around the virtual instruction touches
	 * no memory, so we should be able to use it even here.
	 * Note that in this version, R1 and R2 get "clobbered"; see
	 * vm_ops.S
	 */
	r1 = #VM_TRANS_TYPE_TABLE
	call	__vmnewmap;

	/*  Jump into virtual address range.  */

	r31.h = #hi(__head_s_vaddr_target)
	r31.l = #lo(__head_s_vaddr_target)
	jumpr r31

	/*  Insert trippy space effects.  */

__head_s_vaddr_target:
	/*
	 * Tear down VA=PA translation now that we are running
	 * in kernel virtual space.
	 */
	r0 = #__HVM_PDE_S_INVALID

	r1.h = #0xffc0;
	r1.l = #0x0000;
	r2 = r25;		/* phys_offset */
	r2 = and(r1,r2);

	r1.l = #lo(swapper_pg_dir)
	r1.h = #hi(swapper_pg_dir)
	r2 = lsr(r2, #22)	/* 4MB page number		*/
	r2 = asl(r2, #2)	/* times sizeof(PTE) (4bytes)	*/
	r1 = add(r1,r2);
	loop0(1f,r26)

1:
	{
		memw(R1 ++ #4) = R0
	}:endloop0

	r0 = r24
	r1 = #VM_TRANS_TYPE_TABLE
	call __vmnewmap

	/*  Go ahead and install the trap0 return so angel calls work  */
	r0.h = #hi(_K_provisional_vec)
	r0.l = #lo(_K_provisional_vec)
	call __vmsetvec

	/*
	 * OK, at this point we should start to be much more careful,
	 * we're going to enter C code and start touching memory
	 * in all sorts of places.
	 * This means:
	 *      SGP needs to be OK
	 *	Need to lock shared resources
	 *	A bunch of other things that will cause
	 * 	all kinds of painful bugs
	 */

	/*
	 * Stack pointer should be pointed at the init task's
	 * thread stack, which should have been declared in arch/init_task.c.
	 * So uhhhhh...
	 * It's accessible via the init_thread_union, which is a union
	 * of a thread_info struct and a stack; of course, the top
	 * of the stack is not for you.  The end of the stack
	 * is simply init_thread_union + THREAD_SIZE.
	 */

	{r29.H = #HI(init_thread_union); r0.H = #HI(_THREAD_SIZE); }
	{r29.L = #LO(init_thread_union); r0.L = #LO(_THREAD_SIZE); }

	/*  initialize the register used to point to current_thread_info */
	/*  Fixme:  THREADINFO_REG can't be R2 because of that memset thing. */
	{r29 = add(r29,r0); THREADINFO_REG = r29; }

	/*  Hack:  zero bss; */
	{ r0.L = #LO(__bss_start);  r1 = #0; r2.l = #LO(__bss_stop); }
	{ r0.H = #HI(__bss_start);           r2.h = #HI(__bss_stop); }

	r2 = sub(r2,r0);
	call memset;

	/*  Set PHYS_OFFSET; should be in R25 */
#ifdef CONFIG_HEXAGON_PHYS_OFFSET
	r0.l = #LO(__phys_offset);
	r0.h = #HI(__phys_offset);
	memw(r0) = r25;
#endif

	/* Time to make the doughnuts.   */
	call start_kernel

	/*
	 * Should not reach here.
	 */
1:
	jump 1b

.p2align PAGE_SHIFT
ENTRY(external_cmdline_buffer)
        .fill _PAGE_SIZE,1,0

.data
.p2align PAGE_SHIFT
ENTRY(empty_zero_page)
        .fill _PAGE_SIZE,1,0
