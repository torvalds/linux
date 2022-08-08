/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 FORTH-ICS/CARV
 *  Nick Kossifidis <mick@ics.forth.gr>
 */

#include <asm/asm.h>	/* For RISCV_* and REG_* macros */
#include <asm/csr.h>	/* For CSR_* macros */
#include <asm/page.h>	/* For PAGE_SIZE */
#include <linux/linkage.h> /* For SYM_* macros */

.section ".rodata"
SYM_CODE_START(riscv_kexec_relocate)

	/*
	 * s0: Pointer to the current entry
	 * s1: (const) Phys address to jump to after relocation
	 * s2: (const) Phys address of the FDT image
	 * s3: (const) The hartid of the current hart
	 * s4: Pointer to the destination address for the relocation
	 * s5: (const) Number of words per page
	 * s6: (const) 1, used for subtraction
	 * s7: (const) va_pa_offset, used when switching MMU off
	 * s8: (const) Physical address of the main loop
	 * s9: (debug) indirection page counter
	 * s10: (debug) entry counter
	 * s11: (debug) copied words counter
	 */
	mv	s0, a0
	mv	s1, a1
	mv	s2, a2
	mv	s3, a3
	mv	s4, zero
	li	s5, (PAGE_SIZE / RISCV_SZPTR)
	li	s6, 1
	mv	s7, a4
	mv	s8, zero
	mv	s9, zero
	mv	s10, zero
	mv	s11, zero

	/* Disable / cleanup interrupts */
	csrw	CSR_SIE, zero
	csrw	CSR_SIP, zero

	/*
	 * When we switch SATP.MODE to "Bare" we'll only
	 * play with physical addresses. However the first time
	 * we try to jump somewhere, the offset on the jump
	 * will be relative to pc which will still be on VA. To
	 * deal with this we set stvec to the physical address at
	 * the start of the loop below so that we jump there in
	 * any case.
	 */
	la	s8, 1f
	sub	s8, s8, s7
	csrw	CSR_STVEC, s8

	/* Process entries in a loop */
.align 2
1:
	addi	s10, s10, 1
	REG_L	t0, 0(s0)		/* t0 = *image->entry */
	addi	s0, s0, RISCV_SZPTR	/* image->entry++ */

	/* IND_DESTINATION entry ? -> save destination address */
	andi	t1, t0, 0x1
	beqz	t1, 2f
	andi	s4, t0, ~0x1
	j	1b

2:
	/* IND_INDIRECTION entry ? -> update next entry ptr (PA) */
	andi	t1, t0, 0x2
	beqz	t1, 2f
	andi	s0, t0, ~0x2
	addi	s9, s9, 1
	csrw	CSR_SATP, zero
	jalr	zero, s8, 0

2:
	/* IND_DONE entry ? -> jump to done label */
	andi	t1, t0, 0x4
	beqz	t1, 2f
	j	4f

2:
	/*
	 * IND_SOURCE entry ? -> copy page word by word to the
	 * destination address we got from IND_DESTINATION
	 */
	andi	t1, t0, 0x8
	beqz	t1, 1b		/* Unknown entry type, ignore it */
	andi	t0, t0, ~0x8
	mv	t3, s5		/* i = num words per page */
3:	/* copy loop */
	REG_L	t1, (t0)	/* t1 = *src_ptr */
	REG_S	t1, (s4)	/* *dst_ptr = *src_ptr */
	addi	t0, t0, RISCV_SZPTR /* stc_ptr++ */
	addi	s4, s4, RISCV_SZPTR /* dst_ptr++ */
	sub	t3, t3, s6	/* i-- */
	addi	s11, s11, 1	/* c++ */
	beqz	t3, 1b		/* copy done ? */
	j	3b

4:
	/* Pass the arguments to the next kernel  / Cleanup*/
	mv	a0, s3
	mv	a1, s2
	mv	a2, s1

	/* Cleanup */
	mv	a3, zero
	mv	a4, zero
	mv	a5, zero
	mv	a6, zero
	mv	a7, zero

	mv	s0, zero
	mv	s1, zero
	mv	s2, zero
	mv	s3, zero
	mv	s4, zero
	mv	s5, zero
	mv	s6, zero
	mv	s7, zero
	mv	s8, zero
	mv	s9, zero
	mv	s10, zero
	mv	s11, zero

	mv	t0, zero
	mv	t1, zero
	mv	t2, zero
	mv	t3, zero
	mv	t4, zero
	mv	t5, zero
	mv	t6, zero
	csrw	CSR_SEPC, zero
	csrw	CSR_SCAUSE, zero
	csrw	CSR_SSCRATCH, zero

	/*
	 * Make sure the relocated code is visible
	 * and jump to the new kernel
	 */
	fence.i

	jalr	zero, a2, 0

SYM_CODE_END(riscv_kexec_relocate)
riscv_kexec_relocate_end:


/* Used for jumping to crashkernel */
.section ".text"
SYM_CODE_START(riscv_kexec_norelocate)
	/*
	 * s0: (const) Phys address to jump to
	 * s1: (const) Phys address of the FDT image
	 * s2: (const) The hartid of the current hart
	 * s3: (const) va_pa_offset, used when switching MMU off
	 */
	mv	s0, a1
	mv	s1, a2
	mv	s2, a3
	mv	s3, a4

	/* Disable / cleanup interrupts */
	csrw	CSR_SIE, zero
	csrw	CSR_SIP, zero

	/* Switch to physical addressing */
	la	s4, 1f
	sub	s4, s4, s3
	csrw	CSR_STVEC, s4
	csrw	CSR_SATP, zero

.align 2
1:
	/* Pass the arguments to the next kernel  / Cleanup*/
	mv	a0, s2
	mv	a1, s1
	mv	a2, s0

	/* Cleanup */
	mv	a3, zero
	mv	a4, zero
	mv	a5, zero
	mv	a6, zero
	mv	a7, zero

	mv	s0, zero
	mv	s1, zero
	mv	s2, zero
	mv	s3, zero
	mv	s4, zero
	mv	s5, zero
	mv	s6, zero
	mv	s7, zero
	mv	s8, zero
	mv	s9, zero
	mv	s10, zero
	mv	s11, zero

	mv	t0, zero
	mv	t1, zero
	mv	t2, zero
	mv	t3, zero
	mv	t4, zero
	mv	t5, zero
	mv	t6, zero
	csrw	CSR_SEPC, zero
	csrw	CSR_SCAUSE, zero
	csrw	CSR_SSCRATCH, zero

	jalr	zero, a2, 0
SYM_CODE_END(riscv_kexec_norelocate)

.section ".rodata"
SYM_DATA(riscv_kexec_relocate_size,
	.long riscv_kexec_relocate_end - riscv_kexec_relocate)

