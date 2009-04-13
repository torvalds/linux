/*
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/page.h>

	.text
ENTRY(_start)
	mfs	r1, rmsr
	andi	r1, r1, ~2
	mts	rmsr, r1

/* save fdt to kernel location */
/* r7 stores pointer to fdt blob */
	beqi	r7, no_fdt_arg
	or	r11, r0, r0 /* incremment */
	ori	r4, r0, TOPHYS(_fdt_start) /* save bram context */
	ori	r3, r0, (0x4000 - 4)
_copy_fdt:
	lw	r12, r7, r11 /* r12 = r7 + r11 */
	sw	r12, r4, r11 /* addr[r4 + r11] = r12 */
	addik	r11, r11, 4 /* increment counting */
	bgtid	r3, _copy_fdt /* loop for all entries */
	addik	r3, r3, -4 /* descrement loop */
no_fdt_arg:

	/* Initialize small data anchors */
	la	r13, r0, _KERNEL_SDA_BASE_
	la	r2, r0, _KERNEL_SDA2_BASE_

	/* Initialize stack pointer */
	la	r1, r0, init_thread_union + THREAD_SIZE - 4

	/* Initialize r31 with current task address */
	la	r31, r0, init_task

	/*
	 * Call platform dependent initialize function.
	 * Please see $(ARCH)/mach-$(SUBARCH)/setup.c for
	 * the function.
	 */
	la	r8, r0, machine_early_init
	brald	r15, r8
	nop

	la	r15, r0, machine_halt
	braid	start_kernel
	nop
