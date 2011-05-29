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

	mfs	r1, rmsr
	andi	r1, r1, ~2
	mts	rmsr, r1
/*
 * According to Xilinx, msrclr instruction behaves like 'mfs rX,rpc'
 * if the msrclr instruction is not enabled. We use this to detect
 * if the opcode is available, by issuing msrclr and then testing the result.
 * r8 == 0 - msr instructions are implemented
 * r8 != 0 - msr instructions are not implemented
 */
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
	ori	r3, r0, (0x4000 - 4)
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
 * copy command line to __init_end. There is space for storing command line.
 */
	or	r6, r0, r0		/* incremment */
	ori	r4, r0, __init_end	/* load address of command line */
	tophys(r4,r4)			/* convert to phys address */
	ori	r3, r0, COMMAND_LINE_SIZE - 1 /* number of loops */
_copy_command_line:
	lbu	r2, r5, r6 /* r2=r5+r6 - r5 contain pointer to command line */
	sb	r2, r4, r6		/* addr[r4+r6]= r2*/
	addik	r6, r6, 1		/* increment counting */
	bgtid	r3, _copy_command_line	/* loop for all entries       */
	addik	r3, r3, -1		/* descrement loop */
	addik	r5, r4, 0		/* add new space for command line */
	tovirt(r5,r5)
#endif /* CONFIG_CMDLINE_BOOL */

#ifdef NOT_COMPILE
/* save bram context */
	or	r6, r0, r0				/* incremment */
	ori	r4, r0, TOPHYS(_bram_load_start)	/* save bram context */
	ori	r3, r0, (LMB_SIZE - 4)
_copy_bram:
	lw	r7, r0, r6		/* r7 = r0 + r6 */
	sw	r7, r4, r6		/* addr[r4 + r6] = r7*/
	addik	r6, r6, 4		/* increment counting */
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

	/*
	 * Configure and load two entries into TLB slots 0 and 1.
	 * In case we are pinning TLBs, these are reserved in by the
	 * other TLB functions.  If not reserving, then it doesn't
	 * matter where they are loaded.
	 */
	andi	r4,r4,0xfffffc00	/* Mask off the real page number */
	ori	r4,r4,(TLB_WR | TLB_EX)	/* Set the write and execute bits */

	andi	r3,r3,0xfffffc00	/* Mask off the effective page number */
	ori	r3,r3,(TLB_VALID | TLB_PAGESZ(PAGESZ_16M))

	mts     rtlbx,r0		/* TLB slow 0 */

	mts	rtlblo,r4		/* Load the data portion of the entry */
	mts	rtlbhi,r3		/* Load the tag portion of the entry */

	addik	r4, r4, 0x01000000	/* Map next 16 M entries */
	addik	r3, r3, 0x01000000

	ori	r6,r0,1			/* TLB slot 1 */
	mts     rtlbx,r6

	mts	rtlblo,r4		/* Load the data portion of the entry */
	mts	rtlbhi,r3		/* Load the tag portion of the entry */

	/*
	 * Load a TLB entry for LMB, since we need access to
	 * the exception vectors, using a 4k real==virtual mapping.
	 */
	ori	r6,r0,3			/* TLB slot 3 */
	mts     rtlbx,r6

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

	/*
	 * Call platform dependent initialize function.
	 * Please see $(ARCH)/mach-$(SUBARCH)/setup.c for
	 * the function.
	 */
	addik	r9, r0, machine_early_init
	brald	r15, r9
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
	# Keep entry 0 and 1 valid. Entry 3 mapped to LMB can go away.
	ori	r5,r0,3
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
