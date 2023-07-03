/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Hibernation low level support for RISCV.
 *
 * Copyright (C) 2023 StarFive Technology Co., Ltd.
 *
 * Author: Jee Heng Sia <jeeheng.sia@starfivetech.com>
 */

#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/assembler.h>
#include <asm/csr.h>

#include <linux/linkage.h>

/*
 * int __hibernate_cpu_resume(void)
 * Switch back to the hibernated image's page table prior to restoring the CPU
 * context.
 *
 * Always returns 0
 */
ENTRY(__hibernate_cpu_resume)
	/* switch to hibernated image's page table. */
	csrw CSR_SATP, s0
	sfence.vma

	REG_L	a0, hibernate_cpu_context

	suspend_restore_regs

	/* Return zero value. */
	mv	a0, zero

	ret
END(__hibernate_cpu_resume)

/*
 * Prepare to restore the image.
 * a0: satp of saved page tables.
 * a1: satp of temporary page tables.
 * a2: cpu_resume.
 */
ENTRY(hibernate_restore_image)
	mv	s0, a0
	mv	s1, a1
	mv	s2, a2
	REG_L	s4, restore_pblist
	REG_L	a1, relocated_restore_code

	jr	a1
END(hibernate_restore_image)

/*
 * The below code will be executed from a 'safe' page.
 * It first switches to the temporary page table, then starts to copy the pages
 * back to the original memory location. Finally, it jumps to __hibernate_cpu_resume()
 * to restore the CPU context.
 */
ENTRY(hibernate_core_restore_code)
	/* switch to temp page table. */
	csrw satp, s1
	sfence.vma
.Lcopy:
	/* The below code will restore the hibernated image. */
	REG_L	a1, HIBERN_PBE_ADDR(s4)
	REG_L	a0, HIBERN_PBE_ORIG(s4)

	copy_page a0, a1

	REG_L	s4, HIBERN_PBE_NEXT(s4)
	bnez	s4, .Lcopy

	jr	s2
END(hibernate_core_restore_code)
