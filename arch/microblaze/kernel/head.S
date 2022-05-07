/*
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * MMU code derived from arch/ppc/kernel/head_4xx.S:
 *    Copyright (c) 1995-1996 Gary Thomas <gdt@linuxppc.org>
 *      Initial PowerPC version.
 *    Copyright (c) 1996 Cort Dougan <cort@cs.nmt.edu>
 *      Rewritten for PReP
 *    Copyright (c) 1996 Paul Mackerras <paulus@cs.anu.edu.au>
 *      Low-level exception handers, MMU support, and rewrite.
 *    Copyright (c) 1997 Dan Malek <dmalek@jlc.net>
 *      PowerPC 8xx modifications.
 *    Copyright (c) 1998-1999 TiVo, Inc.
 *      PowerPC 403GCX modifications.
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *      PowerPC 403GCX/405GP modifications.
 *    Copyright 2000 MontaVista Software Inc.
 *	PPC405 modifications
 *      PowerPC 403GCX/405GP modifications.
 * 	Author: MontaVista Software, Inc.
 *         	frank_rowand@mvista.com or source@mvista.com
 * 	   	debbie_chu@mvista.com
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <linux/of_fdt.h>		/* for OF_DT_HEADER */

#ifdef CONFIG_MMU
#include <asm/setup.h> /* COMMAND_LINE_SIZE */
#include <asm/mmu.h>
#include <asm/processor.h>

.section .data
.global empty_zero_page
.align 12
empty_zero_page:
	.space	PAGE_SIZE
.global swapper_pg_dir
swapper_pg_dir:
	.space	PAGE_SIZE

#endif /* CONFIG_MMU */

.section .rodata
.align 4
endian_check:
	.word	1

	__HEAD
ENTRY(_start)
#if CONFIG_KERNEL_BASE_ADDR == 0
	brai	TOPHYS(real_start)
	.org	0x100
real_start:
#endif

	mts	rmsr, r0
/* Disable stack protection from bootloader */
	mts	rslr, r0
	addi	r8, r0, 0xFFFFFFFF
	mts	rshr, r8
/*
 * According to Xilinx, msrclr instruction behaves like 'mfs rX,rpc'
 * if the msrclr instruction is not enabled. We use this to detect
 * if the opcode is available, by issuing msrclr and then testing the result.
 * r8 == 0 - msr instructions are implemented
 * r8 != 0 - msr instructions are not implemented
 */
	mfs	r1, rmsr
	msrclr	r8, 0 /* clear nothing - just read msr for test */
	cmpu	r8, r8, r1 /* r1 must contain msr reg content */

/* r7 may point to an FDT, or there may be one linked in.
   if it's in r7, we've got to save it away ASAP.
   We ensure r7 points to a valid FDT, just in case the bootloader
   is broken or non-existent */
	beqi	r7, no_fdt_arg			/* NULL pointer?  don't copy */
/* Does r7 point to a valid FDT? Load HEADER magic number */
	/* Run time Big/Little endian platform */
	/* Save 1 as word and load byte - 0 - BIG, 1 - LITTLE */
	lbui	r11, r0, TOPHYS(endian_check)
	beqid	r11, big_endian /* DO NOT break delay stop dependency */
	lw	r11, r0, r7 /* Big endian load in delay slot */
	lwr	r11, r0, r7 /* Little endian load */
big_endian:
	rsubi	r11, r11, OF_DT_HEADER	/* Check FDT header */
	beqi	r11, _prepare_copy_fdt
	or	r7, r0, r0		/* clear R7 when not valid DTB */
	bnei	r11, no_fdt_arg			/* No - get out of here */
_prepare_copy_fdt:
	or	r11, r0, r0 /* incremment */
	ori	r4, r0, TOPHYS(_fdt_start)
	ori	r3, r0, (0x10000 - 4)
_copy_fdt:
	lw	r12, r7, r11 /* r12 = r7 + r11 */
	sw	r12, r4, r11 /* addr[r4 + r11] = r12 */
	addik	r11, r11, 4 /* increment counting */
	bgtid	r3, _copy_fdt /* loop for all entries */
	addik	r3, r3, -4 /* descrement loop */
no_fdt_arg:

#ifdef CONFIG_MMU

#ifndef CONFIG_CMDLINE_BOOL
/*
 * handling command line
 * copy command line directly to cmd_line placed in data section.
 */
	beqid	r5, skip	/* Skip if NULL pointer */
	or	r11, r0, r0		/* incremment */
	ori	r4, r0, cmd_line	/* load address of command line */
	tophys(r4,r4)			/* convert to phys address */
	ori	r3, r0, COMMAND_LINE_SIZE - 1 /* number of loops */
_copy_command_line:
	/* r2=r5+r11 - r5 contain pointer to command line */
	lbu	r2, r5, r11
	beqid	r2, skip		/* Skip if no data */
	sb	r2, r4, r11		/* addr[r4+r11]= r2 */
	addik	r11, r11, 1		/* increment counting */
	bgtid	r3, _copy_command_line	/* loop for all entries       */
	addik	r3, r3, -1		/* decrement loop */
	addik	r5, r4, 0		/* add new space for command line */
	tovirt(r5,r5)
skip:
#endif /* CONFIG_CMDLINE_BOOL */

#ifdef NOT_COMPILE
/* save bram context */
	or	r11, r0, r0				/* incremment */
	ori	r4, r0, TOPHYS(_bram_load_start)	/* save bram context */
	ori	r3, r0, (LMB_SIZE - 4)
_copy_bram:
	lw	r7, r0, r11		/* r7 = r0 + r11 */
	sw	r7, r4, r11		/* addr[r4 + r11] = r7 */
	addik	r11, r11, 4		/* increment counting */
	bgtid	r3, _copy_bram		/* loop for all entries */
	addik	r3, r3, -4		/* descrement loop */
#endif
	/* We have to turn on the MMU right away. */

	/*
	 * Set up the initial MMU state so we can do the first level of
	 * kernel initialization.  This maps the first 16 MBytes of memory 1:1
	 * virtual to physical.
	 */
	nop
	addik	r3, r0, MICROBLAZE_TLB_SIZE -1	/* Invalidate all TLB entries */
_invalidate:
	mts	rtlbx, r3
	mts	rtlbhi, r0			/* flush: ensure V is clear   */
	mts	rtlblo, r0
	bgtid	r3, _invalidate		/* loop for all entries       */
	addik	r3, r3, -1
	/* sync */

	/* Setup the kernel PID */
	mts	rpid,r0			/* Load the kernel PID */
	nop
	bri	4

	/*
	 * We should still be executing code at physical address area
	 * RAM_BASEADDR at this point. However, kernel code is at
	 * a virtual address. So, set up a TLB mapping to cover this once
	 * translation is enabled.
	 */

	addik	r3,r0, CONFIG_KERNEL_START /* Load the kernel virtual address */
	tophys(r4,r3)			/* Load the kernel physical address */

	/* start to do TLB calculation */
	addik	r12, r0, _end
	rsub	r12, r3, r12
	addik	r12, r12, CONFIG_LOWMEM_SIZE >> PTE_SHIFT /* that's the pad */

	or r9, r0, r0 /* TLB0 = 0 */
	or r10, r0, r0 /* TLB1 = 0 */

	addik	r11, r12, -0x1000000
	bgei	r11, GT16 /* size is greater than 16MB */
	addik	r11, r12, -0x0800000
	bgei	r11, GT8 /* size is greater than 8MB */
	addik	r11, r12, -0x0400000
	bgei	r11, GT4 /* size is greater than 4MB */
	/* size is less than 4MB */
	addik	r11, r12, -0x0200000
	bgei	r11, GT2 /* size is greater than 2MB */
	addik	r9, r0, 0x0100000 /* TLB0 must be 1MB */
	addik	r11, r12, -0x0100000
	bgei	r11, GT1 /* size is greater than 1MB */
	/* TLB1 is 0 which is setup above */
	bri tlb_end
GT4: /* r11 contains the rest - will be either 1 or 4 */
	ori r9, r0, 0x400000 /* TLB0 is 4MB */
	bri TLB1
GT16: /* TLB0 is 16MB */
	addik	r9, r0, 0x1000000 /* means TLB0 is 16MB */
TLB1:
	/* must be used r2 because of subtract if failed */
	addik	r2, r11, -0x0400000
	bgei	r2, GT20 /* size is greater than 16MB */
	/* size is >16MB and <20MB */
	addik	r11, r11, -0x0100000
	bgei	r11, GT17 /* size is greater than 17MB */
	/* kernel is >16MB and < 17MB */
GT1:
	addik	r10, r0, 0x0100000 /* means TLB1 is 1MB */
	bri tlb_end
GT2: /* TLB0 is 0 and TLB1 will be 4MB */
GT17: /* TLB1 is 4MB - kernel size <20MB */
	addik	r10, r0, 0x0400000 /* means TLB1 is 4MB */
	bri tlb_end
GT8: /* TLB0 is still zero that's why I can use only TLB1 */
GT20: /* TLB1 is 16MB - kernel size >20MB */
	addik	r10, r0, 0x1000000 /* means TLB1 is 16MB */
tlb_end:

	/*
	 * Configure and load two entries into TLB slots 0 and 1.
	 * In case we are pinning TLBs, these are reserved in by the
	 * other TLB functions.  If not reserving, then it doesn't
	 * matter where they are loaded.
	 */
	andi	r4,r4,0xfffffc00	/* Mask off the real page number */
	ori	r4,r4,(TLB_WR | TLB_EX)	/* Set the write and execute bits */

	/*
	 * TLB0 is always used - check if is not zero (r9 stores TLB0 value)
	 * if is use TLB1 value and clear it (r10 stores TLB1 value)
	 */
	bnei	r9, tlb0_not_zero
	add	r9, r10, r0
	add	r10, r0, r0
tlb0_not_zero:

	/* look at the code below */
	ori	r30, r0, 0x200
	andi	r29, r9, 0x100000
	bneid	r29, 1f
	addik	r30, r30, 0x80
	andi	r29, r9, 0x400000
	bneid	r29, 1f
	addik	r30, r30, 0x80
	andi	r29, r9, 0x1000000
	bneid	r29, 1f
	addik	r30, r30, 0x80
1:
	andi	r3,r3,0xfffffc00	/* Mask off the effective page number */
	ori	r3,r3,(TLB_VALID)
	or	r3, r3, r30

	/* Load tlb_skip size value which is index to first unused TLB entry */
	lwi	r11, r0, TOPHYS(tlb_skip)
	mts     rtlbx,r11		/* TLB slow 0 */

	mts	rtlblo,r4		/* Load the data portion of the entry */
	mts	rtlbhi,r3		/* Load the tag portion of the entry */

	/* Increase tlb_skip size */
	addik	r11, r11, 1
	swi	r11, r0, TOPHYS(tlb_skip)

	/* TLB1 can be zeroes that's why we not setup it */
	beqi	r10, jump_over2

	/* look at the code below */
	ori	r30, r0, 0x200
	andi	r29, r10, 0x100000
	bneid	r29, 1f
	addik	r30, r30, 0x80
	andi	r29, r10, 0x400000
	bneid	r29, 1f
	addik	r30, r30, 0x80
	andi	r29, r10, 0x1000000
	bneid	r29, 1f
	addik	r30, r30, 0x80
1:
	addk	r4, r4, r9	/* previous addr + TLB0 size */
	addk	r3, r3, r9

	andi	r3,r3,0xfffffc00	/* Mask off the effective page number */
	ori	r3,r3,(TLB_VALID)
	or	r3, r3, r30

	lwi	r11, r0, TOPHYS(tlb_skip)
	mts     rtlbx, r11		/* r11 is used from TLB0 */

	mts	rtlblo,r4		/* Load the data portion of the entry */
	mts	rtlbhi,r3		/* Load the tag portion of the entry */

	/* Increase tlb_skip size */
	addik	r11, r11, 1
	swi	r11, r0, TOPHYS(tlb_skip)

jump_over2:
	/*
	 * Load a TLB entry for LMB, since we need access to
	 * the exception vectors, using a 4k real==virtual mapping.
	 */
	/* Use temporary TLB_ID for LMB - clear this temporary mapping later */
	ori	r11, r0, MICROBLAZE_LMB_TLB_ID
	mts     rtlbx,r11

	ori	r4,r0,(TLB_WR | TLB_EX)
	ori	r3,r0,(TLB_VALID | TLB_PAGESZ(PAGESZ_4K))

	mts	rtlblo,r4		/* Load the data portion of the entry */
	mts	rtlbhi,r3		/* Load the tag portion of the entry */

	/*
	 * We now have the lower 16 Meg of RAM mapped into TLB entries, and the
	 * caches ready to work.
	 */
turn_on_mmu:
	ori	r15,r0,start_here
	ori	r4,r0,MSR_KERNEL_VMS
	mts	rmsr,r4
	nop
	rted	r15,0			/* enables MMU */
	nop

start_here:
#endif /* CONFIG_MMU */

	/* Initialize small data anchors */
	addik	r13, r0, _KERNEL_SDA_BASE_
	addik	r2, r0, _KERNEL_SDA2_BASE_

	/* Initialize stack pointer */
	addik	r1, r0, init_thread_union + THREAD_SIZE - 4

	/* Initialize r31 with current task address */
	addik	r31, r0, init_task

	addik	r11, r0, machine_early_init
	brald	r15, r11
	nop

#ifndef CONFIG_MMU
	addik	r15, r0, machine_halt
	braid	start_kernel
	nop
#else
	/*
	 * Initialize the MMU.
	 */
	bralid	r15, mmu_init
	nop

	/* Go back to running unmapped so we can load up new values
	 * and change to using our exception vectors.
	 * On the MicroBlaze, all we invalidate the used TLB entries to clear
	 * the old 16M byte TLB mappings.
	 */
	ori	r15,r0,TOPHYS(kernel_load_context)
	ori	r4,r0,MSR_KERNEL
	mts	rmsr,r4
	nop
	bri	4
	rted	r15,0
	nop

	/* Load up the kernel context */
kernel_load_context:
	ori	r5, r0, MICROBLAZE_LMB_TLB_ID
	mts     rtlbx,r5
	nop
	mts	rtlbhi,r0
	nop
	addi	r15, r0, machine_halt
	ori	r17, r0, start_kernel
	ori	r4, r0, MSR_KERNEL_VMS
	mts	rmsr, r4
	nop
	rted	r17, 0		/* enable MMU and jump to start_kernel */
	nop
#endif /* CONFIG_MMU */
