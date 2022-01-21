/* SPDX-License-Identifier: GPL-2.0-or-later */

/* TOC (Transfer of Control) handler. */

	.level 1.1

#include <asm/assembly.h>
#include <linux/threads.h>
#include <linux/linkage.h>

	.text
	.import toc_intr,code
	.import toc_stack,data
	.align 16
ENTRY_CFI(toc_handler)
	load32	PA(toc_stack),%sp

#ifdef CONFIG_SMP
	/* get per-cpu toc_stack address. */
	mfctl   %cr30, %r1
	tophys  %r1,%r2			/* task_struct */
	LDREG   TASK_TI_CPU(%r2),%r4	/* cpu */
	load32	PA(__per_cpu_offset),%r1
	LDREGX  %r4(%r1),%r4
	add	%r4,%sp,%sp
#endif

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
