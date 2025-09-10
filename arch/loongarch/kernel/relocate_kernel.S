/* SPDX-License-Identifier: GPL-2.0 */
/*
 * relocate_kernel.S for kexec
 *
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */

#include <linux/kexec.h>

#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/regdef.h>
#include <asm/loongarch.h>
#include <asm/stackframe.h>
#include <asm/addrspace.h>

SYM_CODE_START(relocate_new_kernel)
	UNWIND_HINT_UNDEFINED
	/*
	 * a0: EFI boot flag for the new kernel
	 * a1: Command line pointer for the new kernel
	 * a2: System table pointer for the new kernel
	 * a3: Start address to jump to after relocation
	 * a4: Pointer to the current indirection page entry
	 */
	move		s0, a4

	/*
	 * In case of a kdump/crash kernel, the indirection page is not
	 * populated as the kernel is directly copied to a reserved location
	 */
	beqz		s0, done

process_entry:
	PTR_L		s1, s0, 0
	PTR_ADDI	s0, s0, SZREG

	/* destination page */
	andi		s2, s1, IND_DESTINATION
	beqz		s2, 1f
	li.w		t0, ~0x1
	and		s3, s1, t0	/* store destination addr in s3 */
	b		process_entry

1:
	/* indirection page, update s0	*/
	andi		s2, s1, IND_INDIRECTION
	beqz		s2, 1f
	li.w		t0, ~0x2
	and		s0, s1, t0
	b		process_entry

1:
	/* done page */
	andi		s2, s1, IND_DONE
	beqz		s2, 1f
	b		done

1:
	/* source page */
	andi		s2, s1, IND_SOURCE
	beqz		s2, process_entry
	li.w		t0, ~0x8
	and		s1, s1, t0
	li.w		s5, (1 << _PAGE_SHIFT) / SZREG

copy_word:
	/* copy page word by word */
	REG_L		s4, s1, 0
	REG_S		s4, s3, 0
	PTR_ADDI	s3, s3, SZREG
	PTR_ADDI	s1, s1, SZREG
	LONG_ADDI	s5, s5, -1
	beqz		s5, process_entry
	b		copy_word

done:
	ibar		0
	dbar		0

	/*
	 * Jump to the new kernel,
	 * make sure the values of a0, a1, a2 and a3 are not changed.
	 */
	jr		a3
SYM_CODE_END(relocate_new_kernel)

#ifdef CONFIG_SMP
/*
 * Other CPUs should wait until code is relocated and
 * then start at the entry point from LOONGARCH_IOCSR_MBUF0.
 */
SYM_CODE_START(kexec_smp_wait)
	UNWIND_HINT_UNDEFINED
1:	li.w		t0, 0x100			/* wait for init loop */
2:	addi.w		t0, t0, -1			/* limit mailbox access */
	bnez		t0, 2b
	li.w		t1, LOONGARCH_IOCSR_MBUF0
	iocsrrd.w	s0, t1				/* check PC as an indicator */
	beqz		s0, 1b
	iocsrrd.d	s0, t1				/* get PC via mailbox */

	li.d		t0, CACHE_BASE
	or		s0, s0, t0			/* s0 = TO_CACHE(s0) */
	jr		s0				/* jump to initial PC */
SYM_CODE_END(kexec_smp_wait)
#endif

relocate_new_kernel_end:

	.section ".data"
SYM_DATA(relocate_new_kernel_size, .quad relocate_new_kernel_end - relocate_new_kernel)
