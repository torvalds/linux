/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/init.h>
#include <linux/threads.h>

#include <asm/addrspace.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/regdef.h>
#include <asm/loongarch.h>
#include <asm/stackframe.h>

	__REF

SYM_CODE_START(kernel_entry)			# kernel entry point

	/* Config direct window and set PG */
	li.d		t0, CSR_DMW0_INIT	# UC, PLV0, 0x8000 xxxx xxxx xxxx
	csrwr		t0, LOONGARCH_CSR_DMWIN0
	li.d		t0, CSR_DMW1_INIT	# CA, PLV0, 0x9000 xxxx xxxx xxxx
	csrwr		t0, LOONGARCH_CSR_DMWIN1

	/* We might not get launched at the address the kernel is linked to,
	   so we jump there.  */
	la.abs		t0, 0f
	jr		t0
0:
	/* Enable PG */
	li.w		t0, 0xb0		# PLV=0, IE=0, PG=1
	csrwr		t0, LOONGARCH_CSR_CRMD
	li.w		t0, 0x04		# PLV=0, PIE=1, PWE=0
	csrwr		t0, LOONGARCH_CSR_PRMD
	li.w		t0, 0x00		# FPE=0, SXE=0, ASXE=0, BTE=0
	csrwr		t0, LOONGARCH_CSR_EUEN

	la		t0, __bss_start		# clear .bss
	st.d		zero, t0, 0
	la		t1, __bss_stop - LONGSIZE
1:
	addi.d		t0, t0, LONGSIZE
	st.d		zero, t0, 0
	bne		t0, t1, 1b

	la		t0, fw_arg0
	st.d		a0, t0, 0		# firmware arguments
	la		t0, fw_arg1
	st.d		a1, t0, 0

	/* KSave3 used for percpu base, initialized as 0 */
	csrwr		zero, PERCPU_BASE_KS
	/* GPR21 used for percpu base (runtime), initialized as 0 */
	move		u0, zero

	la		tp, init_thread_union
	/* Set the SP after an empty pt_regs.  */
	PTR_LI		sp, (_THREAD_SIZE - 32 - PT_SIZE)
	PTR_ADD		sp, sp, tp
	set_saved_sp	sp, t0, t1
	PTR_ADDI	sp, sp, -4 * SZREG	# init stack pointer

	bl		start_kernel

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

	la.abs		t0, 0f
	jr		t0
0:
	/* Enable PG */
	li.w		t0, 0xb0		# PLV=0, IE=0, PG=1
	csrwr		t0, LOONGARCH_CSR_CRMD
	li.w		t0, 0x04		# PLV=0, PIE=1, PWE=0
	csrwr		t0, LOONGARCH_CSR_PRMD
	li.w		t0, 0x00		# FPE=0, SXE=0, ASXE=0, BTE=0
	csrwr		t0, LOONGARCH_CSR_EUEN

	la.abs		t0, cpuboot_data
	ld.d		sp, t0, CPU_BOOT_STACK
	ld.d		tp, t0, CPU_BOOT_TINFO

	bl		start_secondary
SYM_CODE_END(smpboot_entry)

#endif /* CONFIG_SMP */

SYM_ENTRY(kernel_entry_end, SYM_L_GLOBAL, SYM_A_NONE)
