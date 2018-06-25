#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/csr.h>

	.altmacro
	.macro fixup op reg addr lbl
	LOCAL _epc
_epc:
	\op \reg, \addr
	.section __ex_table,"a"
	.balign RISCV_SZPTR
	RISCV_PTR _epc, \lbl
	.previous
	.endm

ENTRY(__asm_copy_to_user)
ENTRY(__asm_copy_from_user)

	/* Enable access to user memory */
	li t6, SR_SUM
	csrs sstatus, t6

	add a3, a1, a2
	/* Use word-oriented copy only if low-order bits match */
	andi t0, a0, SZREG-1
	andi t1, a1, SZREG-1
	bne t0, t1, 2f

	addi t0, a1, SZREG-1
	andi t1, a3, ~(SZREG-1)
	andi t0, t0, ~(SZREG-1)
	/*
	 * a3: terminal address of source region
	 * t0: lowest XLEN-aligned address in source
	 * t1: highest XLEN-aligned address in source
	 */
	bgeu t0, t1, 2f
	bltu a1, t0, 4f
1:
	fixup REG_L, t2, (a1), 10f
	fixup REG_S, t2, (a0), 10f
	addi a1, a1, SZREG
	addi a0, a0, SZREG
	bltu a1, t1, 1b
2:
	bltu a1, a3, 5f

3:
	/* Disable access to user memory */
	csrc sstatus, t6
	li a0, 0
	ret
4: /* Edge case: unalignment */
	fixup lbu, t2, (a1), 10f
	fixup sb, t2, (a0), 10f
	addi a1, a1, 1
	addi a0, a0, 1
	bltu a1, t0, 4b
	j 1b
5: /* Edge case: remainder */
	fixup lbu, t2, (a1), 10f
	fixup sb, t2, (a0), 10f
	addi a1, a1, 1
	addi a0, a0, 1
	bltu a1, a3, 5b
	j 3b
ENDPROC(__asm_copy_to_user)
ENDPROC(__asm_copy_from_user)


ENTRY(__clear_user)

	/* Enable access to user memory */
	li t6, SR_SUM
	csrs sstatus, t6

	add a3, a0, a1
	addi t0, a0, SZREG-1
	andi t1, a3, ~(SZREG-1)
	andi t0, t0, ~(SZREG-1)
	/*
	 * a3: terminal address of target region
	 * t0: lowest doubleword-aligned address in target region
	 * t1: highest doubleword-aligned address in target region
	 */
	bgeu t0, t1, 2f
	bltu a0, t0, 4f
1:
	fixup REG_S, zero, (a0), 11f
	addi a0, a0, SZREG
	bltu a0, t1, 1b
2:
	bltu a0, a3, 5f

3:
	/* Disable access to user memory */
	csrc sstatus, t6
	li a0, 0
	ret
4: /* Edge case: unalignment */
	fixup sb, zero, (a0), 11f
	addi a0, a0, 1
	bltu a0, t0, 4b
	j 1b
5: /* Edge case: remainder */
	fixup sb, zero, (a0), 11f
	addi a0, a0, 1
	bltu a0, a3, 5b
	j 3b
ENDPROC(__clear_user)

	.section .fixup,"ax"
	.balign 4
	/* Fixup code for __copy_user(10) and __clear_user(11) */
10:
	/* Disable access to user memory */
	csrs sstatus, t6
	mv a0, a2
	ret
11:
	csrs sstatus, t6
	mv a0, a1
	ret
	.previous
