/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 Waldorf Electronics
 * Written by Ralf Baechle and Andreas Busse
 * Copyright (C) 1994 - 99, 2003, 06 Ralf Baechle
 * Copyright (C) 1996 Paul M. Antoine
 * Modified for DECStation and hence R3000 support by Paul M. Antoine
 * Further modifications by David S. Miller and Harald Koerfgen
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Kevin Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/init.h>
#include <linux/threads.h>

#include <asm/addrspace.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/irqflags.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>

#include <kernel-entry-init.h>

	/*
	 * For the moment disable interrupts, mark the kernel mode and
	 * set ST0_KX so that the CPU does not spit fire when using
	 * 64-bit addresses.  A full initialization of the CPU's status
	 * register is done later in per_cpu_trap_init().
	 */
	.macro	setup_c0_status set clr
	.set	push
	mfc0	t0, CP0_STATUS
	or	t0, ST0_CU0|\set|0x1f|\clr
	xor	t0, 0x1f|\clr
	mtc0	t0, CP0_STATUS
	.set	noreorder
	sll	zero,3				# ehb
	.set	pop
	.endm

	.macro	setup_c0_status_pri
#ifdef CONFIG_64BIT
	setup_c0_status ST0_KX 0
#else
	setup_c0_status 0 0
#endif
	.endm

	.macro	setup_c0_status_sec
#ifdef CONFIG_64BIT
	setup_c0_status ST0_KX ST0_BEV
#else
	setup_c0_status 0 ST0_BEV
#endif
	.endm

#ifndef CONFIG_NO_EXCEPT_FILL
	/*
	 * Reserved space for exception handlers.
	 * Necessary for machines which link their kernels at KSEG0.
	 */
	.fill	0x400
#endif

EXPORT(_stext)

#ifdef CONFIG_BOOT_RAW
	/*
	 * Give us a fighting chance of running if execution beings at the
	 * kernel load address.	 This is needed because this platform does
	 * not have a ELF loader yet.
	 */
FEXPORT(__kernel_entry)
	j	kernel_entry
#endif /* CONFIG_BOOT_RAW */

	__REF

NESTED(kernel_entry, 16, sp)			# kernel entry point

	kernel_entry_setup			# cpu specific setup

	setup_c0_status_pri

	/* We might not get launched at the address the kernel is linked to,
	   so we jump there.  */
	PTR_LA	t0, 0f
	jr	t0
0:

#ifdef CONFIG_USE_OF
#if defined(CONFIG_MIPS_RAW_APPENDED_DTB) || \
	defined(CONFIG_MIPS_ELF_APPENDED_DTB)

	PTR_LA		t2, __appended_dtb

#ifdef CONFIG_CPU_BIG_ENDIAN
	li		t1, 0xd00dfeed
#else  /* !CONFIG_CPU_BIG_ENDIAN */
	li		t1, 0xedfe0dd0
#endif /* !CONFIG_CPU_BIG_ENDIAN */
	lw		t0, (t2)
	beq		t0, t1, dtb_found
#endif /* CONFIG_MIPS_RAW_APPENDED_DTB || CONFIG_MIPS_ELF_APPENDED_DTB */
	li		t1, -2
	move		t2, a1
	beq		a0, t1, dtb_found

	li		t2, 0
dtb_found:
#endif /* CONFIG_USE_OF */
	PTR_LA		t0, __bss_start		# clear .bss
	LONG_S		zero, (t0)
	PTR_LA		t1, __bss_stop - LONGSIZE
1:
	PTR_ADDIU	t0, LONGSIZE
	LONG_S		zero, (t0)
	bne		t0, t1, 1b

	LONG_S		a0, fw_arg0		# firmware arguments
	LONG_S		a1, fw_arg1
	LONG_S		a2, fw_arg2
	LONG_S		a3, fw_arg3

#ifdef CONFIG_USE_OF
	LONG_S		t2, fw_passed_dtb
#endif

	MTC0		zero, CP0_CONTEXT	# clear context register
#ifdef CONFIG_64BIT
	MTC0		zero, CP0_XCONTEXT
#endif
	PTR_LA		$28, init_thread_union
	/* Set the SP after an empty pt_regs.  */
	PTR_LI		sp, _THREAD_SIZE - 32 - PT_SIZE
	PTR_ADDU	sp, $28
	back_to_back_c0_hazard
	set_saved_sp	sp, t0, t1
	PTR_SUBU	sp, 4 * SZREG		# init stack pointer

#ifdef CONFIG_RELOCATABLE
	/* Copy kernel and apply the relocations */
	jal		relocate_kernel

	/* Repoint the sp into the new kernel image */
	PTR_LI		sp, _THREAD_SIZE - 32 - PT_SIZE
	PTR_ADDU	sp, $28
	set_saved_sp	sp, t0, t1
	PTR_SUBU	sp, 4 * SZREG		# init stack pointer

	/*
	 * relocate_kernel returns the entry point either
	 * in the relocated kernel or the original if for
	 * some reason relocation failed - jump there now
	 * with instruction hazard barrier because of the
	 * newly sync'd icache.
	 */
	jr.hb		v0
#else  /* !CONFIG_RELOCATABLE */
	j		start_kernel
#endif /* !CONFIG_RELOCATABLE */
	END(kernel_entry)

#ifdef CONFIG_SMP
/*
 * SMP slave cpus entry point.	Board specific code for bootstrap calls this
 * function after setting up the stack and gp registers.
 */
NESTED(smp_bootstrap, 16, sp)
	smp_slave_setup
	setup_c0_status_sec
	j	start_secondary
	END(smp_bootstrap)
#endif /* CONFIG_SMP */
