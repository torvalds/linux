/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/init.h>
#include <linux/threads.h>

#include <asm/addrspace.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/bug.h>
#include <asm/regdef.h>
#include <asm/loongarch.h>
#include <asm/stackframe.h>

#ifdef CONFIG_EFI_STUB

#include "efi-header.S"

	__HEAD

_head:
	.word	MZ_MAGIC		/* "MZ", MS-DOS header */
	.org	0x8
	.dword	kernel_entry		/* Kernel entry point */
	.dword	_kernel_asize		/* Kernel image effective size */
	.quad	PHYS_LINK_KADDR		/* Kernel image load offset from start of RAM */
	.org	0x38			/* 0x20 ~ 0x37 reserved */
	.long	LINUX_PE_MAGIC
	.long	pe_header - _head	/* Offset to the PE header */

pe_header:
	__EFI_PE_HEADER

SYM_DATA(kernel_asize, .long _kernel_asize);
SYM_DATA(kernel_fsize, .long _kernel_fsize);

#endif

	__REF

	.align 12

SYM_CODE_START(kernel_entry)			# kernel entry point

	/* Config direct window and set PG */
	li.d		t0, CSR_DMW0_INIT	# UC, PLV0, 0x8000 xxxx xxxx xxxx
	csrwr		t0, LOONGARCH_CSR_DMWIN0
	li.d		t0, CSR_DMW1_INIT	# CA, PLV0, 0x9000 xxxx xxxx xxxx
	csrwr		t0, LOONGARCH_CSR_DMWIN1

	JUMP_VIRT_ADDR	t0, t1

	/* Enable PG */
	li.w		t0, 0xb0		# PLV=0, IE=0, PG=1
	csrwr		t0, LOONGARCH_CSR_CRMD
	li.w		t0, 0x04		# PLV=0, PIE=1, PWE=0
	csrwr		t0, LOONGARCH_CSR_PRMD
	li.w		t0, 0x00		# FPE=0, SXE=0, ASXE=0, BTE=0
	csrwr		t0, LOONGARCH_CSR_EUEN

	la.pcrel	t0, __bss_start		# clear .bss
	st.d		zero, t0, 0
	la.pcrel	t1, __bss_stop - LONGSIZE
1:
	addi.d		t0, t0, LONGSIZE
	st.d		zero, t0, 0
	bne		t0, t1, 1b

	la.pcrel	t0, fw_arg0
	st.d		a0, t0, 0		# firmware arguments
	la.pcrel	t0, fw_arg1
	st.d		a1, t0, 0
	la.pcrel	t0, fw_arg2
	st.d		a2, t0, 0

#ifdef CONFIG_PAGE_SIZE_4KB
	li.d		t0, 0
	li.d		t1, CSR_STFILL
	csrxchg		t0, t1, LOONGARCH_CSR_IMPCTL1
#endif
	/* KSave3 used for percpu base, initialized as 0 */
	csrwr		zero, PERCPU_BASE_KS
	/* GPR21 used for percpu base (runtime), initialized as 0 */
	move		u0, zero

	la.pcrel	tp, init_thread_union
	/* Set the SP after an empty pt_regs.  */
	PTR_LI		sp, (_THREAD_SIZE - PT_SIZE)
	PTR_ADD		sp, sp, tp
	set_saved_sp	sp, t0, t1

#ifdef CONFIG_RELOCATABLE

	bl		relocate_kernel

#ifdef CONFIG_RANDOMIZE_BASE
	/* Repoint the sp into the new kernel */
	PTR_LI		sp, (_THREAD_SIZE - PT_SIZE)
	PTR_ADD		sp, sp, tp
	set_saved_sp	sp, t0, t1

	/* Jump to the new kernel: new_pc = current_pc + random_offset */
	pcaddi		t0, 0
	add.d		t0, t0, a0
	jirl		zero, t0, 0xc
#endif /* CONFIG_RANDOMIZE_BASE */

#endif /* CONFIG_RELOCATABLE */

#ifdef CONFIG_KASAN
	bl		kasan_early_init
#endif

	bl		start_kernel
	ASM_BUG()

SYM_CODE_END(kernel_entry)

#ifdef CONFIG_SMP

/*
 * SMP slave cpus entry point.	Board specific code for bootstrap calls this
 * function after setting up the stack and tp registers.
 */
SYM_CODE_START(smpboot_entry)
	li.d		t0, CSR_DMW0_INIT	# UC, PLV0
	csrwr		t0, LOONGARCH_CSR_DMWIN0
	li.d		t0, CSR_DMW1_INIT	# CA, PLV0
	csrwr		t0, LOONGARCH_CSR_DMWIN1

	JUMP_VIRT_ADDR	t0, t1

#ifdef CONFIG_PAGE_SIZE_4KB
	li.d		t0, 0
	li.d		t1, CSR_STFILL
	csrxchg		t0, t1, LOONGARCH_CSR_IMPCTL1
#endif
	/* Enable PG */
	li.w		t0, 0xb0		# PLV=0, IE=0, PG=1
	csrwr		t0, LOONGARCH_CSR_CRMD
	li.w		t0, 0x04		# PLV=0, PIE=1, PWE=0
	csrwr		t0, LOONGARCH_CSR_PRMD
	li.w		t0, 0x00		# FPE=0, SXE=0, ASXE=0, BTE=0
	csrwr		t0, LOONGARCH_CSR_EUEN

	la.pcrel	t0, cpuboot_data
	ld.d		sp, t0, CPU_BOOT_STACK
	ld.d		tp, t0, CPU_BOOT_TINFO

	bl		start_secondary
	ASM_BUG()

SYM_CODE_END(smpboot_entry)

#endif /* CONFIG_SMP */

SYM_ENTRY(kernel_entry_end, SYM_L_GLOBAL, SYM_A_NONE)
