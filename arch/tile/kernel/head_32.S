/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * TILE startup code.
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/thread_info.h>
#include <asm/processor.h>
#include <asm/asm-offsets.h>
#include <hv/hypervisor.h>
#include <arch/chip.h>
#include <arch/spr_def.h>

/*
 * This module contains the entry code for kernel images. It performs the
 * minimal setup needed to call the generic C routines.
 */

	__HEAD
ENTRY(_start)
	/* Notify the hypervisor of what version of the API we want */
	{
	  movei r1, TILE_CHIP
	  movei r2, TILE_CHIP_REV
	}
	{
	  moveli r0, _HV_VERSION_OLD_HV_INIT
	  jal hv_init
	}
	/* Get a reasonable default ASID in r0 */
	{
	  move r0, zero
	  jal hv_inquire_asid
	}
	/* Install the default page table */
	{
	  moveli r6, lo16(swapper_pgprot - PAGE_OFFSET)
	  move r4, r0     /* use starting ASID of range for this page table */
	}
	{
	  moveli r0, lo16(swapper_pg_dir - PAGE_OFFSET)
	  auli r6, r6, ha16(swapper_pgprot - PAGE_OFFSET)
	}
	{
	  lw r2, r6
	  addi r6, r6, 4
	}
	{
	  lw r3, r6
	  auli r0, r0, ha16(swapper_pg_dir - PAGE_OFFSET)
	}
	{
	  inv r6
	  move r1, zero   /* high 32 bits of CPA is zero */
	}
	{
	  moveli lr, lo16(1f)
	  moveli r5, CTX_PAGE_FLAG
	}
	{
	  auli lr, lr, ha16(1f)
	  j hv_install_context
	}
1:

	/* Get our processor number and save it away in SAVE_K_0. */
	jal hv_inquire_topology
	mulll_uu r4, r1, r2        /* r1 == y, r2 == width */
	add r4, r4, r0             /* r0 == x, so r4 == cpu == y*width + x */

#ifdef CONFIG_SMP
	/*
	 * Load up our per-cpu offset.  When the first (master) tile
	 * boots, this value is still zero, so we will load boot_pc
	 * with start_kernel, and boot_sp with init_stack + THREAD_SIZE.
	 * The master tile initializes the per-cpu offset array, so that
	 * when subsequent (secondary) tiles boot, they will instead load
	 * from their per-cpu versions of boot_sp and boot_pc.
	 */
	moveli r5, lo16(__per_cpu_offset)
	auli r5, r5, ha16(__per_cpu_offset)
	s2a r5, r4, r5
	lw r5, r5
	bnz r5, 1f

	/*
	 * Save the width and height to the smp_topology variable
	 * for later use.
	 */
	moveli r0, lo16(smp_topology + HV_TOPOLOGY_WIDTH_OFFSET)
	auli r0, r0, ha16(smp_topology + HV_TOPOLOGY_WIDTH_OFFSET)
	{
	  sw r0, r2
	  addi r0, r0, (HV_TOPOLOGY_HEIGHT_OFFSET - HV_TOPOLOGY_WIDTH_OFFSET)
	}
	sw r0, r3
1:
#else
	move r5, zero
#endif

	/* Load and go with the correct pc and sp. */
	{
	  addli r1, r5, lo16(boot_sp)
	  addli r0, r5, lo16(boot_pc)
	}
	{
	  auli r1, r1, ha16(boot_sp)
	  auli r0, r0, ha16(boot_pc)
	}
	lw r0, r0
	lw sp, r1
	or r4, sp, r4
	mtspr SPR_SYSTEM_SAVE_K_0, r4  /* save ksp0 + cpu */
	addi sp, sp, -STACK_TOP_DELTA
	{
	  move lr, zero   /* stop backtraces in the called function */
	  jr r0
	}
	ENDPROC(_start)

__PAGE_ALIGNED_BSS
	.align PAGE_SIZE
ENTRY(empty_zero_page)
	.fill PAGE_SIZE,1,0
	END(empty_zero_page)

	.macro PTE va, cpa, bits1, no_org=0
	.ifeq \no_org
	.org swapper_pg_dir + PGD_INDEX(\va) * HV_PTE_SIZE
	.endif
	.word HV_PTE_PAGE | HV_PTE_DIRTY | HV_PTE_PRESENT | HV_PTE_ACCESSED | \
	      (HV_PTE_MODE_CACHE_NO_L3 << HV_PTE_INDEX_MODE)
	.word (\bits1) | (HV_CPA_TO_PTFN(\cpa) << (HV_PTE_INDEX_PTFN - 32))
	.endm

__PAGE_ALIGNED_DATA
	.align PAGE_SIZE
ENTRY(swapper_pg_dir)
	/*
	 * All data pages from PAGE_OFFSET to MEM_USER_INTRPT are mapped as
	 * VA = PA + PAGE_OFFSET.  We remap things with more precise access
	 * permissions and more respect for size of RAM later.
	 */
	.set addr, 0
	.rept (MEM_USER_INTRPT - PAGE_OFFSET) >> PGDIR_SHIFT
	PTE addr + PAGE_OFFSET, addr, (1 << (HV_PTE_INDEX_READABLE - 32)) | \
				      (1 << (HV_PTE_INDEX_WRITABLE - 32))
	.set addr, addr + PGDIR_SIZE
	.endr

	/* The true text VAs are mapped as VA = PA + MEM_SV_INTRPT */
	PTE MEM_SV_INTRPT, 0, (1 << (HV_PTE_INDEX_READABLE - 32)) | \
			      (1 << (HV_PTE_INDEX_EXECUTABLE - 32))
	.org swapper_pg_dir + PGDIR_SIZE
	END(swapper_pg_dir)

	/*
	 * Isolate swapper_pgprot to its own cache line, since each cpu
	 * starting up will read it using VA-is-PA and local homing.
	 * This would otherwise likely conflict with other data on the cache
	 * line, once we have set its permanent home in the page tables.
	 */
	__INITDATA
	.align CHIP_L2_LINE_SIZE()
ENTRY(swapper_pgprot)
	PTE	0, 0, (1 << (HV_PTE_INDEX_READABLE - 32)) | \
		      (1 << (HV_PTE_INDEX_WRITABLE - 32)), 1
	.align CHIP_L2_LINE_SIZE()
	END(swapper_pgprot)
