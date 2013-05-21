/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
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
#if KERNEL_PL == 1 && _HV_VERSION == 13
	  /* Support older hypervisors by asking for API version 12. */
	  movei r0, _HV_VERSION_OLD_HV_INIT
#else
	  movei r0, _HV_VERSION
#endif
	  movei r1, TILE_CHIP
	}
	{
	  movei r2, TILE_CHIP_REV
	  movei r3, KERNEL_PL
	}
	jal hv_init
	/* Get a reasonable default ASID in r0 */
	{
	  move r0, zero
	  jal hv_inquire_asid
	}

	/*
	 * Install the default page table.  The relocation required to
	 * statically define the table is a bit too complex, so we have
	 * to plug in the pointer from the L0 to the L1 table by hand.
	 * We only do this on the first cpu to boot, though, since the
	 * other CPUs should see a properly-constructed page table.
	 */
	{
	  v4int_l r2, zero, r0    /* ASID for hv_install_context */
	  moveli r4, hw1_last(swapper_pgprot - PAGE_OFFSET)
	}
	{
	  shl16insli r4, r4, hw0(swapper_pgprot - PAGE_OFFSET)
	}
	{
	  ld r1, r4               /* access_pte for hv_install_context */
	}
	{
	  moveli r0, hw1_last(.Lsv_data_pmd - PAGE_OFFSET)
	  moveli r6, hw1_last(temp_data_pmd - PAGE_OFFSET)
	}
	{
	  /* After initializing swapper_pgprot, HV_PTE_GLOBAL is set. */
	  bfextu r7, r1, HV_PTE_INDEX_GLOBAL, HV_PTE_INDEX_GLOBAL
	  inv r4
	}
	bnez r7, .Lno_write
	{
	  shl16insli r0, r0, hw0(.Lsv_data_pmd - PAGE_OFFSET)
	  shl16insli r6, r6, hw0(temp_data_pmd - PAGE_OFFSET)
	}
	{
	  /* Cut off the low bits of the PT address. */
	  shrui r6, r6, HV_LOG2_PAGE_TABLE_ALIGN
	  /* Start with our access pte. */
	  move r5, r1
	}
	{
	  /* Stuff the address into the page table pointer slot of the PTE. */
	  bfins r5, r6, HV_PTE_INDEX_PTFN, \
			HV_PTE_INDEX_PTFN + HV_PTE_PTFN_BITS - 1
	}
	{
	  /* Store the L0 data PTE. */
	  st r0, r5
	  addli r6, r6, (temp_code_pmd - temp_data_pmd) >> \
			HV_LOG2_PAGE_TABLE_ALIGN
	}
	{
	  addli r0, r0, .Lsv_code_pmd - .Lsv_data_pmd
	  bfins r5, r6, HV_PTE_INDEX_PTFN, \
			HV_PTE_INDEX_PTFN + HV_PTE_PTFN_BITS - 1
	}
	/* Store the L0 code PTE. */
	st r0, r5

.Lno_write:
	moveli lr, hw2_last(1f)
	{
	  shl16insli lr, lr, hw1(1f)
	  moveli r0, hw1_last(swapper_pg_dir - PAGE_OFFSET)
	}
	{
	  shl16insli lr, lr, hw0(1f)
	  shl16insli r0, r0, hw0(swapper_pg_dir - PAGE_OFFSET)
	}
	{
	  moveli r3, CTX_PAGE_FLAG
	  j hv_install_context
	}
1:

	/* Install the interrupt base. */
	moveli r0, hw2_last(MEM_SV_START)
	shl16insli r0, r0, hw1(MEM_SV_START)
	shl16insli r0, r0, hw0(MEM_SV_START)
	mtspr SPR_INTERRUPT_VECTOR_BASE_K, r0

	/*
	 * Get our processor number and save it away in SAVE_K_0.
	 * Extract stuff from the topology structure: r4 = y, r6 = x,
	 * r5 = width.  FIXME: consider whether we want to just make these
	 * 64-bit values (and if so fix smp_topology write below, too).
	 */
	jal hv_inquire_topology
	{
	  v4int_l r5, zero, r1    /* r5 = width */
	  shrui r4, r0, 32        /* r4 = y */
	}
	{
	  v4int_l r6, zero, r0    /* r6 = x */
	  mul_lu_lu r4, r4, r5
	}
	{
	  add r4, r4, r6          /* r4 == cpu == y*width + x */
	}

#ifdef CONFIG_SMP
	/*
	 * Load up our per-cpu offset.  When the first (master) tile
	 * boots, this value is still zero, so we will load boot_pc
	 * with start_kernel, and boot_sp with init_stack + THREAD_SIZE.
	 * The master tile initializes the per-cpu offset array, so that
	 * when subsequent (secondary) tiles boot, they will instead load
	 * from their per-cpu versions of boot_sp and boot_pc.
	 */
	moveli r5, hw2_last(__per_cpu_offset)
	shl16insli r5, r5, hw1(__per_cpu_offset)
	shl16insli r5, r5, hw0(__per_cpu_offset)
	shl3add r5, r4, r5
	ld r5, r5
	bnez r5, 1f

	/*
	 * Save the width and height to the smp_topology variable
	 * for later use.
	 */
	moveli r0, hw2_last(smp_topology + HV_TOPOLOGY_WIDTH_OFFSET)
	shl16insli r0, r0, hw1(smp_topology + HV_TOPOLOGY_WIDTH_OFFSET)
	shl16insli r0, r0, hw0(smp_topology + HV_TOPOLOGY_WIDTH_OFFSET)
	st r0, r1
1:
#else
	move r5, zero
#endif

	/* Load and go with the correct pc and sp. */
	{
	  moveli r1, hw2_last(boot_sp)
	  moveli r0, hw2_last(boot_pc)
	}
	{
	  shl16insli r1, r1, hw1(boot_sp)
	  shl16insli r0, r0, hw1(boot_pc)
	}
	{
	  shl16insli r1, r1, hw0(boot_sp)
	  shl16insli r0, r0, hw0(boot_pc)
	}
	{
	  add r1, r1, r5
	  add r0, r0, r5
	}
	ld r0, r0
	ld sp, r1
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

	.macro PTE cpa, bits1
	.quad HV_PTE_PAGE | HV_PTE_DIRTY | HV_PTE_PRESENT | HV_PTE_ACCESSED |\
	      HV_PTE_GLOBAL | (HV_PTE_MODE_CACHE_NO_L3 << HV_PTE_INDEX_MODE) |\
	      (\bits1) | (HV_CPA_TO_PTFN(\cpa) << HV_PTE_INDEX_PTFN)
	.endm

__PAGE_ALIGNED_DATA
	.align PAGE_SIZE
ENTRY(swapper_pg_dir)
	.org swapper_pg_dir + PGD_INDEX(PAGE_OFFSET) * HV_PTE_SIZE
.Lsv_data_pmd:
	.quad 0  /* PTE temp_data_pmd - PAGE_OFFSET, 0 */
	.org swapper_pg_dir + PGD_INDEX(MEM_SV_START) * HV_PTE_SIZE
.Lsv_code_pmd:
	.quad 0  /* PTE temp_code_pmd - PAGE_OFFSET, 0 */
	.org swapper_pg_dir + SIZEOF_PGD
	END(swapper_pg_dir)

	.align HV_PAGE_TABLE_ALIGN
ENTRY(temp_data_pmd)
	/*
	 * We fill the PAGE_OFFSET pmd with huge pages with
	 * VA = PA + PAGE_OFFSET.  We remap things with more precise access
	 * permissions later.
	 */
	.set addr, 0
	.rept PTRS_PER_PMD
	PTE addr, HV_PTE_READABLE | HV_PTE_WRITABLE
	.set addr, addr + HPAGE_SIZE
	.endr
	.org temp_data_pmd + SIZEOF_PMD
	END(temp_data_pmd)

	.align HV_PAGE_TABLE_ALIGN
ENTRY(temp_code_pmd)
	/*
	 * We fill the MEM_SV_START pmd with huge pages with
	 * VA = PA + PAGE_OFFSET.  We remap things with more precise access
	 * permissions later.
	 */
	.set addr, 0
	.rept PTRS_PER_PMD
	PTE addr, HV_PTE_READABLE | HV_PTE_EXECUTABLE
	.set addr, addr + HPAGE_SIZE
	.endr
	.org temp_code_pmd + SIZEOF_PMD
	END(temp_code_pmd)

	/*
	 * Isolate swapper_pgprot to its own cache line, since each cpu
	 * starting up will read it using VA-is-PA and local homing.
	 * This would otherwise likely conflict with other data on the cache
	 * line, once we have set its permanent home in the page tables.
	 */
	__INITDATA
	.align CHIP_L2_LINE_SIZE()
ENTRY(swapper_pgprot)
	.quad HV_PTE_PRESENT | (HV_PTE_MODE_CACHE_NO_L3 << HV_PTE_INDEX_MODE)
	.align CHIP_L2_LINE_SIZE()
	END(swapper_pgprot)
