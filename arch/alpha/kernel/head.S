/*
 * alpha/boot/head.S
 *
 * initial boot stuff.. At this point, the bootloader has already
 * switched into OSF/1 PAL-code, and loaded us at the correct address
 * (START_ADDR).  So there isn't much left for us to do: just set up
 * the kernel global pointer and jump to the kernel entry-point.
 */

#include <linux/config.h>
#include <asm/system.h>
#include <asm/asm_offsets.h>

.globl swapper_pg_dir
.globl _stext
swapper_pg_dir=SWAPPER_PGD

	.set noreorder
	.globl	__start
	.ent	__start
_stext:
__start:
	.prologue 0
	br	$27,1f
1:	ldgp	$29,0($27)
	/* We need to get current_task_info loaded up...  */
	lda	$8,init_thread_union
	/* ... and find our stack ... */
	lda	$30,0x4000 - SIZEOF_PT_REGS($8)
	/* ... and then we can start the kernel.  */
	jsr	$26,start_kernel
	call_pal PAL_halt
	.end __start

#ifdef CONFIG_SMP
	.align 3
	.globl	__smp_callin
	.ent	__smp_callin
	/* On entry here from SRM console, the HWPCB of the per-cpu
	   slot for this processor has been loaded.  We've arranged
	   for the UNIQUE value for this process to contain the PCBB
	   of the target idle task.  */
__smp_callin:
	.prologue 1
	ldgp	$29,0($27)	# First order of business, load the GP.

	call_pal PAL_rduniq	# Grab the target PCBB.
	mov	$0,$16		# Install it.
	call_pal PAL_swpctx

	lda	$8,0x3fff	# Find "current".
	bic	$30,$8,$8
	
	jsr	$26,smp_callin
	call_pal PAL_halt
	.end __smp_callin
#endif /* CONFIG_SMP */

	#
	# The following two functions are needed for supporting SRM PALcode
	# on the PC164 (at least), since that PALcode manages the interrupt
	# masking, and we cannot duplicate the effort without causing problems
	#

	.align 3
	.globl	cserve_ena
	.ent	cserve_ena
cserve_ena:
	.prologue 0
	bis	$16,$16,$17
	lda	$16,52($31)
	call_pal PAL_cserve
	ret	($26)
	.end	cserve_ena

	.align 3
	.globl	cserve_dis
	.ent	cserve_dis
cserve_dis:
	.prologue 0
	bis	$16,$16,$17
	lda	$16,53($31)
	call_pal PAL_cserve
	ret	($26)
	.end	cserve_dis

	#
	# It is handy, on occasion, to make halt actually just loop. 
	# Putting it here means we dont have to recompile the whole
	# kernel.
	#

	.align 3
	.globl	halt
	.ent	halt
halt:
	.prologue 0
	call_pal PAL_halt
	.end	halt
