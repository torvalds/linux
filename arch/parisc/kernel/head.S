/* This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999-2007 by Helge Deller <deller@gmx.de>
 * Copyright 1999 SuSE GmbH (Philipp Rumpf)
 * Copyright 1999 Philipp Rumpf (prumpf@tux.org)
 * Copyright 2000 Hewlett Packard (Paul Bame, bame@puffin.external.hp.com)
 * Copyright (C) 2001 Grant Grundler (Hewlett Packard)
 * Copyright (C) 2004 Kyle McMartin <kyle@debian.org>
 *
 * Initial Version 04-23-1999 by Helge Deller <deller@gmx.de>
 */

#include <asm/asm-offsets.h>
#include <asm/psw.h>
#include <asm/pdc.h>
	
#include <asm/assembly.h>
#include <asm/pgtable.h>

#include <linux/linkage.h>
#include <linux/init.h>

	.level	LEVEL

	__INITDATA
ENTRY(boot_args)
	.word 0 /* arg0 */
	.word 0 /* arg1 */
	.word 0 /* arg2 */
	.word 0 /* arg3 */
END(boot_args)

	__HEAD

	.align	4
	.import init_thread_union,data
	.import fault_vector_20,code    /* IVA parisc 2.0 32 bit */
#ifndef CONFIG_64BIT
        .import fault_vector_11,code    /* IVA parisc 1.1 32 bit */
	.import	$global$		/* forward declaration */
#endif /*!CONFIG_64BIT*/
	.export _stext,data		/* Kernel want it this way! */
_stext:
ENTRY(stext)
	.proc
	.callinfo

	/* Make sure sr4-sr7 are set to zero for the kernel address space */
	mtsp	%r0,%sr4
	mtsp	%r0,%sr5
	mtsp	%r0,%sr6
	mtsp	%r0,%sr7

	/* Clear BSS (shouldn't the boot loader do this?) */

	.import __bss_start,data
	.import __bss_stop,data

	load32		PA(__bss_start),%r3
	load32		PA(__bss_stop),%r4
$bss_loop:
	cmpb,<<,n       %r3,%r4,$bss_loop
	stw,ma          %r0,4(%r3)

	/* Save away the arguments the boot loader passed in (32 bit args) */
	load32		PA(boot_args),%r1
	stw,ma          %arg0,4(%r1)
	stw,ma          %arg1,4(%r1)
	stw,ma          %arg2,4(%r1)
	stw,ma          %arg3,4(%r1)

	/* Initialize startup VM. Just map first 8/16 MB of memory */
	load32		PA(swapper_pg_dir),%r4
	mtctl		%r4,%cr24	/* Initialize kernel root pointer */
	mtctl		%r4,%cr25	/* Initialize user root pointer */

#if PT_NLEVELS == 3
	/* Set pmd in pgd */
	load32		PA(pmd0),%r5
	shrd            %r5,PxD_VALUE_SHIFT,%r3	
	ldo		(PxD_FLAG_PRESENT+PxD_FLAG_VALID)(%r3),%r3
	stw		%r3,ASM_PGD_ENTRY*ASM_PGD_ENTRY_SIZE(%r4)
	ldo		ASM_PMD_ENTRY*ASM_PMD_ENTRY_SIZE(%r5),%r4
#else
	/* 2-level page table, so pmd == pgd */
	ldo		ASM_PGD_ENTRY*ASM_PGD_ENTRY_SIZE(%r4),%r4
#endif

	/* Fill in pmd with enough pte directories */
	load32		PA(pg0),%r1
	SHRREG		%r1,PxD_VALUE_SHIFT,%r3
	ldo		(PxD_FLAG_PRESENT+PxD_FLAG_VALID)(%r3),%r3

	ldi		ASM_PT_INITIAL,%r1

1:
	stw		%r3,0(%r4)
	ldo		(PAGE_SIZE >> PxD_VALUE_SHIFT)(%r3),%r3
	addib,>		-1,%r1,1b
#if PT_NLEVELS == 3
	ldo             ASM_PMD_ENTRY_SIZE(%r4),%r4
#else
	ldo             ASM_PGD_ENTRY_SIZE(%r4),%r4
#endif


	/* Now initialize the PTEs themselves */
	ldo		0+_PAGE_KERNEL(%r0),%r3 /* Hardwired 0 phys addr start */
	ldi		(1<<(KERNEL_INITIAL_ORDER-PAGE_SHIFT)),%r11 /* PFN count */
	load32		PA(pg0),%r1

$pgt_fill_loop:
	STREGM          %r3,ASM_PTE_ENTRY_SIZE(%r1)
	ldo		(1<<PFN_PTE_SHIFT)(%r3),%r3 /* add one PFN */
	addib,>		-1,%r11,$pgt_fill_loop
	nop

	/* Load the return address...er...crash 'n burn */
	copy		%r0,%r2

	/* And the RFI Target address too */
	load32		start_parisc,%r11

	/* And the initial task pointer */
	load32		init_thread_union,%r6
	mtctl           %r6,%cr30

	/* And the stack pointer too */
	ldo             THREAD_SZ_ALGN(%r6),%sp

#ifdef CONFIG_SMP
	/* Set the smp rendevous address into page zero.
	** It would be safer to do this in init_smp_config() but
	** it's just way easier to deal with here because
	** of 64-bit function ptrs and the address is local to this file.
	*/
	load32		PA(smp_slave_stext),%r10
	stw		%r10,0x10(%r0)	/* MEM_RENDEZ */
	stw		%r0,0x28(%r0)	/* MEM_RENDEZ_HI - assume addr < 4GB */

	/* FALLTHROUGH */
	.procend

	/*
	** Code Common to both Monarch and Slave processors.
	** Entry:
	**
	**  1.1:	
	**    %r11 must contain RFI target address.
	**    %r25/%r26 args to pass to target function
	**    %r2  in case rfi target decides it didn't like something
	**
	**  2.0w:
	**    %r3  PDCE_PROC address
	**    %r11 RFI target address
	**
	** Caller must init: SR4-7, %sp, %r10, %cr24/25, 
	*/
common_stext:
	.proc
	.callinfo
#else
	/* Clear PDC entry point - we won't use it */
	stw		%r0,0x10(%r0)	/* MEM_RENDEZ */
	stw		%r0,0x28(%r0)	/* MEM_RENDEZ_HI */
#endif /*CONFIG_SMP*/

#ifdef CONFIG_64BIT
	tophys_r1	%sp

	/* Save the rfi target address */
	ldd             TI_TASK-THREAD_SZ_ALGN(%sp), %r10
	tophys_r1       %r10
	std             %r11,  TASK_PT_GR11(%r10)
	/* Switch to wide mode Superdome doesn't support narrow PDC
	** calls.
	*/
1:	mfia            %rp             /* clear upper part of pcoq */
	ldo             2f-1b(%rp),%rp
	depdi           0,31,32,%rp
	bv              (%rp)
	ssm             PSW_SM_W,%r0

        /* Set Wide mode as the "Default" (eg for traps)
        ** First trap occurs *right* after (or part of) rfi for slave CPUs.
        ** Someday, palo might not do this for the Monarch either.
        */
2:
#define MEM_PDC_LO 0x388
#define MEM_PDC_HI 0x35C
	ldw             MEM_PDC_LO(%r0),%r3
	ldw             MEM_PDC_HI(%r0),%r6
	depd            %r6, 31, 32, %r3        /* move to upper word */

	ldo             PDC_PSW(%r0),%arg0              /* 21 */
	ldo             PDC_PSW_SET_DEFAULTS(%r0),%arg1 /* 2 */
	ldo             PDC_PSW_WIDE_BIT(%r0),%arg2     /* 2 */
	load32          PA(stext_pdc_ret), %rp
	bv              (%r3)
	copy            %r0,%arg3

stext_pdc_ret:
	/* restore rfi target address*/
	ldd             TI_TASK-THREAD_SZ_ALGN(%sp), %r10
	tophys_r1       %r10
	ldd             TASK_PT_GR11(%r10), %r11
	tovirt_r1       %sp
#endif
	
	/* PARANOID: clear user scratch/user space SR's */
	mtsp	%r0,%sr0
	mtsp	%r0,%sr1
	mtsp	%r0,%sr2
	mtsp	%r0,%sr3

	/* Initialize Protection Registers */
	mtctl	%r0,%cr8
	mtctl	%r0,%cr9
	mtctl	%r0,%cr12
	mtctl	%r0,%cr13

	/* Initialize the global data pointer */
	loadgp

	/* Set up our interrupt table.  HPMCs might not work after this! 
	 *
	 * We need to install the correct iva for PA1.1 or PA2.0. The
	 * following short sequence of instructions can determine this
	 * (without being illegal on a PA1.1 machine).
	 */
#ifndef CONFIG_64BIT
	ldi		32,%r10
	mtctl		%r10,%cr11
	.level 2.0
	mfctl,w		%cr11,%r10
	.level 1.1
	comib,<>,n	0,%r10,$is_pa20
	ldil		L%PA(fault_vector_11),%r10
	b		$install_iva
	ldo		R%PA(fault_vector_11)(%r10),%r10

$is_pa20:
	.level		LEVEL /* restore 1.1 || 2.0w */
#endif /*!CONFIG_64BIT*/
	load32		PA(fault_vector_20),%r10

$install_iva:
	mtctl		%r10,%cr14

	b		aligned_rfi  /* Prepare to RFI! Man all the cannons! */
	nop

	.align 128
aligned_rfi:
	pcxt_ssm_bug

	rsm		PSW_SM_QUIET,%r0	/* off troublesome PSW bits */
	/* Don't need NOPs, have 8 compliant insn before rfi */

	mtctl		%r0,%cr17	/* Clear IIASQ tail */
	mtctl		%r0,%cr17	/* Clear IIASQ head */

	/* Load RFI target into PC queue */
	mtctl		%r11,%cr18	/* IIAOQ head */
	ldo		4(%r11),%r11
	mtctl		%r11,%cr18	/* IIAOQ tail */

	load32		KERNEL_PSW,%r10
	mtctl		%r10,%ipsw
	
	/* Jump through hyperspace to Virt Mode */
	rfi
	nop

	.procend

#ifdef CONFIG_SMP

	.import smp_init_current_idle_task,data
	.import	smp_callin,code

#ifndef CONFIG_64BIT
smp_callin_rtn:
        .proc
	.callinfo
	break	1,1		/*  Break if returned from start_secondary */
	nop
	nop
        .procend
#endif /*!CONFIG_64BIT*/

/***************************************************************************
* smp_slave_stext is executed by all non-monarch Processors when the Monarch
* pokes the slave CPUs in smp.c:smp_boot_cpus().
*
* Once here, registers values are initialized in order to branch to virtual
* mode. Once all available/eligible CPUs are in virtual mode, all are
* released and start out by executing their own idle task.
*****************************************************************************/
smp_slave_stext:
        .proc
	.callinfo

	/*
	** Initialize Space registers
	*/
	mtsp	   %r0,%sr4
	mtsp	   %r0,%sr5
	mtsp	   %r0,%sr6
	mtsp	   %r0,%sr7

	/*  Initialize the SP - monarch sets up smp_init_current_idle_task */
	load32		PA(smp_init_current_idle_task),%sp
	LDREG		0(%sp),%sp	/* load task address */
	tophys_r1	%sp
	LDREG		TASK_THREAD_INFO(%sp),%sp
	mtctl           %sp,%cr30       /* store in cr30 */
	ldo             THREAD_SZ_ALGN(%sp),%sp

	/* point CPU to kernel page tables */
	load32		PA(swapper_pg_dir),%r4
	mtctl		%r4,%cr24	/* Initialize kernel root pointer */
	mtctl		%r4,%cr25	/* Initialize user root pointer */

#ifdef CONFIG_64BIT
	/* Setup PDCE_PROC entry */
	copy            %arg0,%r3
#else
	/* Load RFI *return* address in case smp_callin bails */
	load32		smp_callin_rtn,%r2
#endif
	
	/* Load RFI target address.  */
	load32		smp_callin,%r11
	
	/* ok...common code can handle the rest */
	b		common_stext
	nop

	.procend
#endif /* CONFIG_SMP */

ENDPROC(stext)

#ifndef CONFIG_64BIT
	.section .data..read_mostly

	.align	4
	.export	$global$,data

	.type	$global$,@object
	.size	$global$,4
$global$:	
	.word 0
#endif /*!CONFIG_64BIT*/
