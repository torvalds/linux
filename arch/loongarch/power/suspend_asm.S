/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Sleep helper for Loongson-3 sleep mode.
 *
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/addrspace.h>
#include <asm/loongarch.h>
#include <asm/stackframe.h>

/* preparatory stuff */
.macro	SETUP_SLEEP
	PTR_ADDI	sp, sp, -PT_SIZE
	REG_S		$r1, sp, PT_R1
	REG_S		$r2, sp, PT_R2
	REG_S		$r3, sp, PT_R3
	REG_S		$r4, sp, PT_R4
	REG_S		$r21, sp, PT_R21
	REG_S		$r22, sp, PT_R22
	REG_S		$r23, sp, PT_R23
	REG_S		$r24, sp, PT_R24
	REG_S		$r25, sp, PT_R25
	REG_S		$r26, sp, PT_R26
	REG_S		$r27, sp, PT_R27
	REG_S		$r28, sp, PT_R28
	REG_S		$r29, sp, PT_R29
	REG_S		$r30, sp, PT_R30
	REG_S		$r31, sp, PT_R31
.endm

.macro SETUP_WAKEUP
	REG_L		$r1, sp, PT_R1
	REG_L		$r2, sp, PT_R2
	REG_L		$r3, sp, PT_R3
	REG_L		$r4, sp, PT_R4
	REG_L		$r21, sp, PT_R21
	REG_L		$r22, sp, PT_R22
	REG_L		$r23, sp, PT_R23
	REG_L		$r24, sp, PT_R24
	REG_L		$r25, sp, PT_R25
	REG_L		$r26, sp, PT_R26
	REG_L		$r27, sp, PT_R27
	REG_L		$r28, sp, PT_R28
	REG_L		$r29, sp, PT_R29
	REG_L		$r30, sp, PT_R30
	REG_L		$r31, sp, PT_R31
	PTR_ADDI	sp, sp, PT_SIZE
.endm

	.text
	.align 12

/* Sleep/wakeup code for Loongson-3 */
SYM_FUNC_START(loongarch_suspend_enter)
	SETUP_SLEEP

	la.pcrel	t0, acpi_saved_sp
	REG_S		sp, t0, 0

	bl		__flush_cache_all

	/* Pass RA and SP to BIOS */
	PTR_ADDI	a1, sp, 0
	la.pcrel	a0, loongarch_wakeup_start
	la.pcrel	t0, loongarch_suspend_addr
	REG_L		t0, t0, 0
	jirl		ra, t0, 0 /* Call BIOS's STR sleep routine */

	/*
	 * This is where we return upon wakeup.
	 * Reload all of the registers and return.
	 */
SYM_INNER_LABEL(loongarch_wakeup_start, SYM_L_GLOBAL)
	SETUP_DMWINS	t0
	JUMP_VIRT_ADDR	t0, t1

	/* Enable PG */
	li.w		t0, 0xb0		# PLV=0, IE=0, PG=1
	csrwr		t0, LOONGARCH_CSR_CRMD

	la.pcrel	t0, acpi_saved_sp
	REG_L		sp, t0, 0

	SETUP_WAKEUP
	jr		ra
SYM_FUNC_END(loongarch_suspend_enter)
