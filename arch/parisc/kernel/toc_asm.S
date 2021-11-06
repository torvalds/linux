/* SPDX-License-Identifier: GPL-2.0-or-later */

/* TOC (Transfer of Control) handler. */

	.level 1.1

#include <asm/assembly.h>
#include <asm/psw.h>
#include <linux/threads.h>
#include <linux/linkage.h>

	.text
	.import toc_intr,code
	.import toc_lock,data
	.align 16
ENTRY_CFI(toc_handler)
	/*
	 * synchronize CPUs and obtain offset
	 * for stack setup.
	 */
	load32		PA(toc_lock),%r1
0:	ldcw,co		0(%r1),%r2
	cmpib,=		0,%r2,0b
	nop
	addi		1,%r2,%r4
	stw		%r4,0(%r1)
	addi		-1,%r2,%r4

	load32	PA(toc_stack),%sp
	/*
	 * deposit CPU number into stack address,
	 * so every CPU will have its own stack.
	 */
	SHLREG	%r4,14,%r4
	add	%r4,%sp,%sp

	/*
	 * setup pt_regs on stack and save the
	 * floating point registers. PIM_TOC doesn't
	 * save fp registers, so we're doing it here.
	 */
	copy	%sp,%arg0
	ldo	PT_SZ_ALGN(%sp), %sp

	/* clear pt_regs */
	copy	%arg0,%r1
0:	cmpb,<<,n %r1,%sp,0b
	stw,ma	%r0,4(%r1)

	ldo	PT_FR0(%arg0),%r25
	save_fp	%r25

	/* go virtual */
	load32	PA(swapper_pg_dir),%r4
	mtctl	%r4,%cr24
	mtctl	%r4,%cr25

	/* Clear sr4-sr7 */
	mtsp	%r0, %sr4
	mtsp	%r0, %sr5
	mtsp	%r0, %sr6
	mtsp	%r0, %sr7

	tovirt_r1 %sp
	tovirt_r1 %arg0
	virt_map

	loadgp

#ifdef CONFIG_64BIT
	ldo	-16(%sp),%r29
#endif
	load32	toc_intr,%r1
	be	0(%sr7,%r1)
	nop
ENDPROC_CFI(toc_handler)

	/*
	 * keep this checksum here, as it is part of the toc_handler
	 * spanned by toc_handler_size (all words in toc_handler are
	 * added in PDC and the sum must equal to zero.
	 */
SYM_DATA(toc_handler_csum, .long 0)
SYM_DATA(toc_handler_size, .long . - toc_handler)

	__PAGE_ALIGNED_BSS
	.align 64
SYM_DATA(toc_stack, .block 16384*NR_CPUS)
