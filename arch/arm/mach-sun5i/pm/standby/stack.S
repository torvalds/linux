#include <mach/platform.h>

/**stack point address in sram*/
#define SP_IN_SRAM	0xf0007ffc

    .text
    .globl save_sp
save_sp:
    mov r0, r13
    ldr  r13, =SP_IN_SRAM
    mov pc,lr

    .text
    .globl restore_sp
restore_sp:
    mov r13, r0
    mov pc,lr


    .text
    .globl standby_flush_tlb
standby_flush_tlb:
    push    {r0-r3}
    /* invalid un-locked data and instruction tlb */
    mcr p15, 0, r0, c8, c7, 0
    pop     {r0-r3}
    mov pc,lr

    .text
    .globl standby_preload_tlb
standby_preload_tlb:
    push    {r0-r3}
    /* preload data tlb for standby io operation */
    ldr r0, =SW_VA_CCM_IO_BASE
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =SW_VA_TWI0_IO_BASE
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =SW_VA_TWI1_IO_BASE
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =SW_VA_TWI2_IO_BASE
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =SW_VA_INT_IO_BASE
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =SW_VA_TIMERC_IO_BASE
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =SW_VA_LRADC_IO_BASE
    mcr p15, 0, r0, c10, c1, 0
    /* preload data tlb for standby code&data area */
    ldr r0, =0xf0000000
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =0xf0000400
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =0xf0000800
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =0xf0000c00
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =0xf0001000
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =0xf0001400
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =0xf0001800
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =0xf0001c00
    mcr p15, 0, r0, c10, c1, 0

    /* preload instruction tlb for standby code&data area */
    ldr r0, =0xf0000000
    mcr p15, 0, r0, c10, c1, 1
    ldr r0, =0xf0000400
    mcr p15, 0, r0, c10, c1, 1
    ldr r0, =0xf0000800
    mcr p15, 0, r0, c10, c1, 1
    ldr r0, =0xf0000c00
    mcr p15, 0, r0, c10, c1, 1
    ldr r0, =0xf0001000
    mcr p15, 0, r0, c10, c1, 1
    ldr r0, =0xf0001400
    mcr p15, 0, r0, c10, c1, 1
    ldr r0, =0xf0001800
    mcr p15, 0, r0, c10, c1, 1
    ldr r0, =0xf0001c00
    mcr p15, 0, r0, c10, c1, 1

    pop     {r0-r3}
    mov pc,lr

