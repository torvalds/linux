#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/cache.h>

#include <mach/iomap.h>

#include "flowctrl.h"
#include "reset.h"
#include "sleep.h"

#define APB_MISC_GP_HIDREV	0x804
#define PMC_SCRATCH41	0x140

#define RESET_DATA(x)	((TEGRA_RESET_##x)*4)

        .section ".text.head", "ax"
	__CPUINIT

/*
 * Tegra specific entry point for secondary CPUs.
 *   The secondary kernel init calls v7_flush_dcache_all before it enables
 *   the L1; however, the L1 comes out of reset in an undefined state, so
 *   the clean + invalidate performed by v7_flush_dcache_all causes a bunch
 *   of cache lines with uninitialized data and uninitialized tags to get
 *   written out to memory, which does really unpleasant things to the main
 *   processor.  We fix this by performing an invalidate, rather than a
 *   clean + invalidate, before jumping into the kernel.
 */
ENTRY(v7_invalidate_l1)
        mov     r0, #0
        mcr     p15, 2, r0, c0, c0, 0
        mrc     p15, 1, r0, c0, c0, 0

        ldr     r1, =0x7fff
        and     r2, r1, r0, lsr #13

        ldr     r1, =0x3ff

        and     r3, r1, r0, lsr #3  @ NumWays - 1
        add     r2, r2, #1          @ NumSets

        and     r0, r0, #0x7
        add     r0, r0, #4          @ SetShift

        clz     r1, r3              @ WayShift
        add     r4, r3, #1          @ NumWays
1:      sub     r2, r2, #1          @ NumSets--
        mov     r3, r4              @ Temp = NumWays
2:      subs    r3, r3, #1          @ Temp--
        mov     r5, r3, lsl r1
        mov     r6, r2, lsl r0
        orr     r5, r5, r6          @ Reg = (Temp<<WayShift)|(NumSets<<SetShift)
        mcr     p15, 0, r5, c7, c6, 2
        bgt     2b
        cmp     r2, #0
        bgt     1b
        dsb
        isb
        mov     pc, lr
ENDPROC(v7_invalidate_l1)


ENTRY(tegra_secondary_startup)
        bl      v7_invalidate_l1
	/* Enable coresight */
	mov32	r0, 0xC5ACCE55
	mcr	p14, 0, r0, c7, c12, 6
        b       secondary_startup
ENDPROC(tegra_secondary_startup)

	.align L1_CACHE_SHIFT
ENTRY(__tegra_cpu_reset_handler_start)

/*
 * __tegra_cpu_reset_handler:
 *
 * Common handler for all CPU reset events.
 *
 * Register usage within the reset handler:
 *
 *      R7  = CPU present (to the OS) mask
 *      R8  = CPU in LP1 state mask
 *      R9  = CPU in LP2 state mask
 *      R10 = CPU number
 *      R11 = CPU mask
 *      R12 = pointer to reset handler data
 *
 * NOTE: This code is copied to IRAM. All code and data accesses
 *       must be position-independent.
 */

	.align L1_CACHE_SHIFT
ENTRY(__tegra_cpu_reset_handler)

	cpsid	aif, 0x13			@ SVC mode, interrupts disabled
	mrc	p15, 0, r10, c0, c0, 5		@ MPIDR
	and	r10, r10, #0x3			@ R10 = CPU number
	mov	r11, #1
	mov	r11, r11, lsl r10  		@ R11 = CPU mask
	adr	r12, __tegra_cpu_reset_handler_data

#ifdef CONFIG_SMP
	/* Does the OS know about this CPU? */
	ldr	r7, [r12, #RESET_DATA(MASK_PRESENT)]
	tst	r7, r11 			@ if !present
	bleq	__die				@ CPU not present (to OS)
#endif

#ifdef CONFIG_ARCH_TEGRA_2x_SOC
	/* Are we on Tegra20? */
	mov32	r6, TEGRA_APB_MISC_BASE
	ldr	r0, [r6, #APB_MISC_GP_HIDREV]
	and	r0, r0, #0xff00
	cmp	r0, #(0x20 << 8)
	bne	1f
	/* If not CPU0, don't let CPU0 reset CPU1 now that CPU1 is coming up. */
	mov32	r6, TEGRA_PMC_BASE
	mov	r0, #0
	cmp	r10, #0
	strne	r0, [r6, #PMC_SCRATCH41]
1:
#endif

#ifdef CONFIG_SMP
	/*
	 * Can only be secondary boot (initial or hotplug) but CPU 0
	 * cannot be here.
	 */
	cmp	r10, #0
	bleq	__die				@ CPU0 cannot be here
	ldr	lr, [r12, #RESET_DATA(STARTUP_SECONDARY)]
	cmp	lr, #0
	bleq	__die				@ no secondary startup handler
	bx	lr
#endif

/*
 * We don't know why the CPU reset. Just kill it.
 * The LR register will contain the address we died at + 4.
 */

__die:
	sub	lr, lr, #4
	mov32	r7, TEGRA_PMC_BASE
	str	lr, [r7, #PMC_SCRATCH41]

	mov32	r7, TEGRA_CLK_RESET_BASE

	/* Are we on Tegra20? */
	mov32	r6, TEGRA_APB_MISC_BASE
	ldr	r0, [r6, #APB_MISC_GP_HIDREV]
	and	r0, r0, #0xff00
	cmp	r0, #(0x20 << 8)
	bne	1f

#ifdef CONFIG_ARCH_TEGRA_2x_SOC
	mov32	r0, 0x1111
	mov	r1, r0, lsl r10
	str	r1, [r7, #0x340]		@ CLK_RST_CPU_CMPLX_SET
#endif
1:
#ifdef CONFIG_ARCH_TEGRA_3x_SOC
	mov32	r6, TEGRA_FLOW_CTRL_BASE

	cmp	r10, #0
	moveq	r1, #FLOW_CTRL_HALT_CPU0_EVENTS
	moveq	r2, #FLOW_CTRL_CPU0_CSR
	movne	r1, r10, lsl #3
	addne	r2, r1, #(FLOW_CTRL_CPU1_CSR-8)
	addne	r1, r1, #(FLOW_CTRL_HALT_CPU1_EVENTS-8)

	/* Clear CPU "event" and "interrupt" flags and power gate
	   it when halting but not before it is in the "WFI" state. */
	ldr	r0, [r6, +r2]
	orr	r0, r0, #FLOW_CTRL_CSR_INTR_FLAG | FLOW_CTRL_CSR_EVENT_FLAG
	orr	r0, r0, #FLOW_CTRL_CSR_ENABLE
	str	r0, [r6, +r2]

	/* Unconditionally halt this CPU */
	mov	r0, #FLOW_CTRL_WAITEVENT
	str	r0, [r6, +r1]
	ldr	r0, [r6, +r1]			@ memory barrier

	dsb
	isb
	wfi					@ CPU should be power gated here

	/* If the CPU didn't power gate above just kill it's clock. */

	mov	r0, r11, lsl #8
	str	r0, [r7, #348]			@ CLK_CPU_CMPLX_SET
#endif

	/* If the CPU still isn't dead, just spin here. */
	b	.
ENDPROC(__tegra_cpu_reset_handler)

	.align L1_CACHE_SHIFT
	.type	__tegra_cpu_reset_handler_data, %object
	.globl	__tegra_cpu_reset_handler_data
__tegra_cpu_reset_handler_data:
	.rept	TEGRA_RESET_DATA_SIZE
	.long	0
	.endr
	.align L1_CACHE_SHIFT

ENTRY(__tegra_cpu_reset_handler_end)
