#include <linux/linkage.h>
#include <linux/threads.h>
#include <asm/asm-offsets.h>
#include <asm/assembler.h>
#include <asm/glue-cache.h>
#include <asm/glue-proc.h>
#include <asm/system.h>
	.text

/*
 * Save CPU state for a suspend
 *  r0 = phys addr of temporary page tables
 *  r1 = v:p offset
 *  r2 = suspend function arg0
 *  r3 = suspend function
 */
ENTRY(__cpu_suspend)
	stmfd	sp!, {r4 - r11, lr}
	mov	r4, r0
#ifdef MULTI_CPU
	ldr	r10, =processor
	ldr	r5, [r10, #CPU_SLEEP_SIZE] @ size of CPU sleep state
	ldr	ip, [r10, #CPU_DO_RESUME] @ virtual resume function
#else
	ldr	r5, =cpu_suspend_size
	ldr	ip, =cpu_do_resume
#endif
	mov	r6, sp			@ current virtual SP
	sub	sp, sp, r5		@ allocate CPU state on stack
	mov	r0, sp			@ save pointer to CPU save block
	add	ip, ip, r1		@ convert resume fn to phys
	stmfd	sp!, {r4, r6, ip}	@ save phys pgd, virt SP, phys resume fn
	ldr	r5, =sleep_save_sp
	add	r6, sp, r1		@ convert SP to phys
	stmfd	sp!, {r2, r3}		@ save suspend func arg and pointer
#ifdef CONFIG_SMP
	ALT_SMP(mrc p15, 0, lr, c0, c0, 5)
	ALT_UP(mov lr, #0)
	and	lr, lr, #15
	str	r6, [r5, lr, lsl #2]	@ save phys SP
#else
	str	r6, [r5]		@ save phys SP
#endif
#ifdef MULTI_CPU
	mov	lr, pc
	ldr	pc, [r10, #CPU_DO_SUSPEND] @ save CPU state
#else
	bl	cpu_do_suspend
#endif

	@ flush data cache
#ifdef MULTI_CACHE
	ldr	r10, =cpu_cache
	mov	lr, pc
	ldr	pc, [r10, #CACHE_FLUSH_KERN_ALL]
#else
	bl	__cpuc_flush_kern_all
#endif
	adr	lr, BSYM(cpu_suspend_abort)
	ldmfd	sp!, {r0, pc}		@ call suspend fn
ENDPROC(__cpu_suspend)
	.ltorg

cpu_suspend_abort:
	ldmia	sp!, {r1 - r3}		@ pop phys pgd, virt SP, phys resume fn
	teq	r0, #0
	moveq	r0, #1			@ force non-zero value
	mov	sp, r2
	ldmfd	sp!, {r4 - r11, pc}
ENDPROC(cpu_suspend_abort)

/*
 * r0 = control register value
 */
	.align	5
ENTRY(cpu_resume_mmu)
	ldr	r3, =cpu_resume_after_mmu
	mcr	p15, 0, r0, c1, c0, 0	@ turn on MMU, I-cache, etc
	mrc	p15, 0, r0, c0, c0, 0	@ read id reg
	mov	r0, r0
	mov	r0, r0
	mov	pc, r3			@ jump to virtual address
ENDPROC(cpu_resume_mmu)
cpu_resume_after_mmu:
	bl	cpu_init		@ restore the und/abt/irq banked regs
	mov	r0, #0			@ return zero on success
	ldmfd	sp!, {r4 - r11, pc}
ENDPROC(cpu_resume_after_mmu)

/*
 * Note: Yes, part of the following code is located into the .data section.
 *       This is to allow sleep_save_sp to be accessed with a relative load
 *       while we can't rely on any MMU translation.  We could have put
 *       sleep_save_sp in the .text section as well, but some setups might
 *       insist on it to be truly read-only.
 */
	.data
	.align
ENTRY(cpu_resume)
#ifdef CONFIG_SMP
	adr	r0, sleep_save_sp
	ALT_SMP(mrc p15, 0, r1, c0, c0, 5)
	ALT_UP(mov r1, #0)
	and	r1, r1, #15
	ldr	r0, [r0, r1, lsl #2]	@ stack phys addr
#else
	ldr	r0, sleep_save_sp	@ stack phys addr
#endif
	setmode	PSR_I_BIT | PSR_F_BIT | SVC_MODE, r1  @ set SVC, irqs off
	@ load phys pgd, stack, resume fn
  ARM(	ldmia	r0!, {r1, sp, pc}	)
THUMB(	ldmia	r0!, {r1, r2, r3}	)
THUMB(	mov	sp, r2			)
THUMB(	bx	r3			)
ENDPROC(cpu_resume)

sleep_save_sp:
	.rept	CONFIG_NR_CPUS
	.long	0				@ preserve stack phys ptr here
	.endr
