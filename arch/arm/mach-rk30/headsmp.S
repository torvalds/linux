#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/memory.h>

        .section ".text.head", "ax"
        __CPUINIT

/*
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
        mcr     p15, 0, r0, c7, c5, 0   @ invalidate I cache
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

ENTRY(rk30_secondary_startup)
        bl      v7_invalidate_l1
        b       secondary_startup
ENDPROC(rk30_secondary_startup)

ENTRY(rk30_sram_secondary_startup)
        ldr     pc, 1f
        .word   0xdeadbeaf
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
        ldr     pc, 1f
1:	.long   rk30_secondary_startup - PAGE_OFFSET + PLAT_PHYS_OFFSET
ENDPROC(rk30_sram_secondary_startup)
