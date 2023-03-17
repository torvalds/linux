/* SPDX-License-Identifier: GPL-2.0+ */
#include <linux/linkage.h>
#include <asm/stackframe.h>

	.text

	.macro save_all_base_regs
	cfi_st  ra, PT_R1
	cfi_st	tp, PT_R2
	cfi_st	a0, PT_R4
	cfi_st	a1, PT_R5
	cfi_st	a2, PT_R6
	cfi_st	a3, PT_R7
	cfi_st	a4, PT_R8
	cfi_st	a5, PT_R9
	cfi_st	a6, PT_R10
	cfi_st	a7, PT_R11
	cfi_st	t0, PT_R12
	cfi_st	t1, PT_R13
	cfi_st	t2, PT_R14
	cfi_st	t3, PT_R15
	cfi_st	t4, PT_R16
	cfi_st	t5, PT_R17
	cfi_st	t6, PT_R18
	cfi_st	t7, PT_R19
	cfi_st	t8, PT_R20
	cfi_st	u0, PT_R21
	cfi_st	fp, PT_R22
	cfi_st	s0, PT_R23
	cfi_st	s1, PT_R24
	cfi_st	s2, PT_R25
	cfi_st	s3, PT_R26
	cfi_st	s4, PT_R27
	cfi_st	s5, PT_R28
	cfi_st	s6, PT_R29
	cfi_st	s7, PT_R30
	cfi_st	s8, PT_R31
	csrrd	t0, LOONGARCH_CSR_CRMD
	andi	t0, t0, 0x7 /* extract bit[1:0] PLV, bit[2] IE */
	LONG_S	t0, sp, PT_CRMD
	.endm

	.macro restore_all_base_regs
	cfi_ld	tp, PT_R2
	cfi_ld	a0, PT_R4
	cfi_ld	a1, PT_R5
	cfi_ld	a2, PT_R6
	cfi_ld	a3, PT_R7
	cfi_ld	a4, PT_R8
	cfi_ld	a5, PT_R9
	cfi_ld	a6, PT_R10
	cfi_ld	a7, PT_R11
	cfi_ld	t0, PT_R12
	cfi_ld	t1, PT_R13
	cfi_ld	t2, PT_R14
	cfi_ld	t3, PT_R15
	cfi_ld	t4, PT_R16
	cfi_ld	t5, PT_R17
	cfi_ld	t6, PT_R18
	cfi_ld	t7, PT_R19
	cfi_ld	t8, PT_R20
	cfi_ld	u0, PT_R21
	cfi_ld	fp, PT_R22
	cfi_ld	s0, PT_R23
	cfi_ld	s1, PT_R24
	cfi_ld	s2, PT_R25
	cfi_ld	s3, PT_R26
	cfi_ld	s4, PT_R27
	cfi_ld	s5, PT_R28
	cfi_ld	s6, PT_R29
	cfi_ld	s7, PT_R30
	cfi_ld	s8, PT_R31
	LONG_L  t0, sp, PT_CRMD
	li.d	t1, 0x7 /* mask bit[1:0] PLV, bit[2] IE */
	csrxchg t0, t1, LOONGARCH_CSR_CRMD
	.endm

SYM_CODE_START(__kretprobe_trampoline)
	addi.d	sp, sp, -PT_SIZE
	save_all_base_regs

	addi.d	t0, sp, PT_SIZE
	LONG_S	t0, sp, PT_R3

	move a0, sp /* pt_regs */

	bl trampoline_probe_handler

	/* use the result as the return-address */
	move ra, a0

	restore_all_base_regs
	addi.d	sp, sp, PT_SIZE

	jr ra
SYM_CODE_END(__kretprobe_trampoline)
