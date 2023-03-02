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
	addi.d		sp, sp, -PT_SIZE
	st.d		$r1, sp, PT_R1
	st.d		$r2, sp, PT_R2
	st.d		$r3, sp, PT_R3
	st.d		$r4, sp, PT_R4
	st.d		$r21, sp, PT_R21
	st.d		$r22, sp, PT_R22
	st.d		$r23, sp, PT_R23
	st.d		$r24, sp, PT_R24
	st.d		$r25, sp, PT_R25
	st.d		$r26, sp, PT_R26
	st.d		$r27, sp, PT_R27
	st.d		$r28, sp, PT_R28
	st.d		$r29, sp, PT_R29
	st.d		$r30, sp, PT_R30
	st.d		$r31, sp, PT_R31

	la.pcrel	t0, acpi_saved_sp
	st.d		sp, t0, 0
.endm

.macro SETUP_WAKEUP
	ld.d		$r1, sp, PT_R1
	ld.d		$r2, sp, PT_R2
	ld.d		$r3, sp, PT_R3
	ld.d		$r4, sp, PT_R4
	ld.d		$r21, sp, PT_R21
	ld.d		$r22, sp, PT_R22
	ld.d		$r23, sp, PT_R23
	ld.d		$r24, sp, PT_R24
	ld.d		$r25, sp, PT_R25
	ld.d		$r26, sp, PT_R26
	ld.d		$r27, sp, PT_R27
	ld.d		$r28, sp, PT_R28
	ld.d		$r29, sp, PT_R29
	ld.d		$r30, sp, PT_R30
	ld.d		$r31, sp, PT_R31
.endm

	.text
	.align 12

/* Sleep/wakeup code for Loongson-3 */
SYM_FUNC_START(loongarch_suspend_enter)
	SETUP_SLEEP
	bl		__flush_cache_all

	/* Pass RA and SP to BIOS */
	addi.d		a1, sp, 0
	la.pcrel	a0, loongarch_wakeup_start
	la.pcrel	t0, loongarch_suspend_addr
	ld.d		t0, t0, 0
	jirl		a0, t0, 0 /* Call BIOS's STR sleep routine */

	/*
	 * This is where we return upon wakeup.
	 * Reload all of the registers and return.
	 */
SYM_INNER_LABEL(loongarch_wakeup_start, SYM_L_GLOBAL)
	li.d		t0, CSR_DMW0_INIT	# UC, PLV0
	csrwr		t0, LOONGARCH_CSR_DMWIN0
	li.d		t0, CSR_DMW1_INIT	# CA, PLV0
	csrwr		t0, LOONGARCH_CSR_DMWIN1

	JUMP_VIRT_ADDR	t0, t1

	la.pcrel	t0, acpi_saved_sp
	ld.d		sp, t0, 0
	SETUP_WAKEUP
	addi.d		sp, sp, PT_SIZE
	jr		ra
SYM_FUNC_END(loongarch_suspend_enter)
