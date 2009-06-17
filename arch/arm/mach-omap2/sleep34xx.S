/*
 * linux/arch/arm/mach-omap2/sleep.S
 *
 * (C) Copyright 2007
 * Texas Instruments
 * Karthik Dasu <karthik-dp@ti.com>
 *
 * (C) Copyright 2004
 * Texas Instruments, <www.ti.com>
 * Richard Woodruff <r-woodruff2@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR /PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <mach/io.h>
#include <mach/control.h>

#include "prm.h"
#include "sdrc.h"

#define PM_PREPWSTST_CORE_V	OMAP34XX_PRM_REGADDR(CORE_MOD, \
				OMAP3430_PM_PREPWSTST)
#define PM_PREPWSTST_MPU_V	OMAP34XX_PRM_REGADDR(MPU_MOD, \
				OMAP3430_PM_PREPWSTST)
#define PM_PWSTCTRL_MPU_P	OMAP34XX_PRM_REGADDR(MPU_MOD, PM_PWSTCTRL)
#define SCRATCHPAD_MEM_OFFS	0x310 /* Move this as correct place is
				       * available */
#define SCRATCHPAD_BASE_P	OMAP343X_CTRL_REGADDR(\
				OMAP343X_CONTROL_MEM_WKUP +\
				SCRATCHPAD_MEM_OFFS)
#define SDRC_POWER_V		OMAP34XX_SDRC_REGADDR(SDRC_POWER)

	.text
/* Function call to get the restore pointer for resume from OFF */
ENTRY(get_restore_pointer)
        stmfd   sp!, {lr}     @ save registers on stack
	adr	r0, restore
        ldmfd   sp!, {pc}     @ restore regs and return
ENTRY(get_restore_pointer_sz)
        .word   . - get_restore_pointer_sz
/*
 * Forces OMAP into idle state
 *
 * omap34xx_suspend() - This bit of code just executes the WFI
 * for normal idles.
 *
 * Note: This code get's copied to internal SRAM at boot. When the OMAP
 *	 wakes up it continues execution at the point it went to sleep.
 */
ENTRY(omap34xx_cpu_suspend)
	stmfd	sp!, {r0-r12, lr}		@ save registers on stack
loop:
	/*b	loop*/	@Enable to debug by stepping through code
	/* r0 contains restore pointer in sdram */
	/* r1 contains information about saving context */
	ldr     r4, sdrc_power          @ read the SDRC_POWER register
	ldr     r5, [r4]                @ read the contents of SDRC_POWER
	orr     r5, r5, #0x40           @ enable self refresh on idle req
	str     r5, [r4]                @ write back to SDRC_POWER register

	cmp	r1, #0x0
	/* If context save is required, do that and execute wfi */
	bne	save_context_wfi
	/* Data memory barrier and Data sync barrier */
	mov	r1, #0
	mcr	p15, 0, r1, c7, c10, 4
	mcr	p15, 0, r1, c7, c10, 5

	wfi				@ wait for interrupt

	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	bl i_dll_wait

	ldmfd	sp!, {r0-r12, pc}		@ restore regs and return
restore:
	/* b restore*/ 	@ Enable to debug restore code
        /* Check what was the reason for mpu reset and store the reason in r9*/
        /* 1 - Only L1 and logic lost */
        /* 2 - Only L2 lost - In this case, we wont be here */
        /* 3 - Both L1 and L2 lost */
	ldr     r1, pm_pwstctrl_mpu
	ldr	r2, [r1]
	and     r2, r2, #0x3
	cmp     r2, #0x0	@ Check if target power state was OFF or RET
        moveq   r9, #0x3        @ MPU OFF => L1 and L2 lost
	movne	r9, #0x1	@ Only L1 and L2 lost => avoid L2 invalidation
	bne	logic_l1_restore
	/* Execute smi to invalidate L2 cache */
	mov r12, #0x1                         @ set up to invalide L2
smi:    .word 0xE1600070                @ Call SMI monitor (smieq)
logic_l1_restore:
	mov	r1, #0
	/* Invalidate all instruction caches to PoU
	 * and flush branch target cache */
	mcr	p15, 0, r1, c7, c5, 0

	ldr	r4, scratchpad_base
	ldr	r3, [r4,#0xBC]
	ldmia	r3!, {r4-r6}
	mov	sp, r4
	msr	spsr_cxsf, r5
	mov	lr, r6

	ldmia	r3!, {r4-r9}
	/* Coprocessor access Control Register */
	mcr p15, 0, r4, c1, c0, 2

	/* TTBR0 */
	MCR p15, 0, r5, c2, c0, 0
	/* TTBR1 */
	MCR p15, 0, r6, c2, c0, 1
	/* Translation table base control register */
	MCR p15, 0, r7, c2, c0, 2
	/*domain access Control Register */
	MCR p15, 0, r8, c3, c0, 0
	/* data fault status Register */
	MCR p15, 0, r9, c5, c0, 0

	ldmia  r3!,{r4-r8}
	/* instruction fault status Register */
	MCR p15, 0, r4, c5, c0, 1
	/*Data Auxiliary Fault Status Register */
	MCR p15, 0, r5, c5, c1, 0
	/*Instruction Auxiliary Fault Status Register*/
	MCR p15, 0, r6, c5, c1, 1
	/*Data Fault Address Register */
	MCR p15, 0, r7, c6, c0, 0
	/*Instruction Fault Address Register*/
	MCR p15, 0, r8, c6, c0, 2
	ldmia  r3!,{r4-r7}

	/* user r/w thread and process ID */
	MCR p15, 0, r4, c13, c0, 2
	/* user ro thread and process ID */
	MCR p15, 0, r5, c13, c0, 3
	/*Privileged only thread and process ID */
	MCR p15, 0, r6, c13, c0, 4
	/* cache size selection */
	MCR p15, 2, r7, c0, c0, 0
	ldmia  r3!,{r4-r8}
	/* Data TLB lockdown registers */
	MCR p15, 0, r4, c10, c0, 0
	/* Instruction TLB lockdown registers */
	MCR p15, 0, r5, c10, c0, 1
	/* Secure or Nonsecure Vector Base Address */
	MCR p15, 0, r6, c12, c0, 0
	/* FCSE PID */
	MCR p15, 0, r7, c13, c0, 0
	/* Context PID */
	MCR p15, 0, r8, c13, c0, 1

	ldmia  r3!,{r4-r5}
	/* primary memory remap register */
	MCR p15, 0, r4, c10, c2, 0
	/*normal memory remap register */
	MCR p15, 0, r5, c10, c2, 1

	/* Restore cpsr */
	ldmia	r3!,{r4}	/*load CPSR from SDRAM*/
	msr	cpsr, r4	/*store cpsr */

	/* Enabling MMU here */
	mrc	p15, 0, r7, c2, c0, 2 /* Read TTBRControl */
	/* Extract N (0:2) bits and decide whether to use TTBR0 or TTBR1*/
	and	r7, #0x7
	cmp	r7, #0x0
	beq	usettbr0
ttbr_error:
	/* More work needs to be done to support N[0:2] value other than 0
	* So looping here so that the error can be detected
	*/
	b	ttbr_error
usettbr0:
	mrc	p15, 0, r2, c2, c0, 0
	ldr	r5, ttbrbit_mask
	and	r2, r5
	mov	r4, pc
	ldr	r5, table_index_mask
	and	r4, r5 /* r4 = 31 to 20 bits of pc */
	/* Extract the value to be written to table entry */
	ldr	r1, table_entry
	add	r1, r1, r4 /* r1 has value to be written to table entry*/
	/* Getting the address of table entry to modify */
	lsr	r4, #18
	add	r2, r4 /* r2 has the location which needs to be modified */
	/* Storing previous entry of location being modified */
	ldr	r5, scratchpad_base
	ldr	r4, [r2]
	str	r4, [r5, #0xC0]
	/* Modify the table entry */
	str	r1, [r2]
	/* Storing address of entry being modified
	 * - will be restored after enabling MMU */
	ldr	r5, scratchpad_base
	str	r2, [r5, #0xC4]

	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 4	@ Flush prefetch buffer
	mcr	p15, 0, r0, c7, c5, 6	@ Invalidate branch predictor array
	mcr	p15, 0, r0, c8, c5, 0	@ Invalidate instruction TLB
	mcr	p15, 0, r0, c8, c6, 0	@ Invalidate data TLB
	/* Restore control register  but dont enable caches here*/
	/* Caches will be enabled after restoring MMU table entry */
	ldmia	r3!, {r4}
	/* Store previous value of control register in scratchpad */
	str	r4, [r5, #0xC8]
	ldr	r2, cache_pred_disable_mask
	and	r4, r2
	mcr	p15, 0, r4, c1, c0, 0

	ldmfd	sp!, {r0-r12, pc}		@ restore regs and return
save_context_wfi:
	/*b	save_context_wfi*/	@ enable to debug save code
	mov	r8, r0 /* Store SDRAM address in r8 */
        /* Check what that target sleep state is:stored in r1*/
        /* 1 - Only L1 and logic lost */
        /* 2 - Only L2 lost */
        /* 3 - Both L1 and L2 lost */
	cmp	r1, #0x2 /* Only L2 lost */
	beq	clean_l2
	cmp	r1, #0x1 /* L2 retained */
	/* r9 stores whether to clean L2 or not*/
	moveq	r9, #0x0 /* Dont Clean L2 */
	movne	r9, #0x1 /* Clean L2 */
l1_logic_lost:
	/* Store sp and spsr to SDRAM */
	mov	r4, sp
	mrs	r5, spsr
	mov	r6, lr
	stmia	r8!, {r4-r6}
	/* Save all ARM registers */
	/* Coprocessor access control register */
	mrc	p15, 0, r6, c1, c0, 2
	stmia	r8!, {r6}
	/* TTBR0, TTBR1 and Translation table base control */
	mrc	p15, 0, r4, c2, c0, 0
	mrc	p15, 0, r5, c2, c0, 1
	mrc	p15, 0, r6, c2, c0, 2
	stmia	r8!, {r4-r6}
	/* Domain access control register, data fault status register,
	and instruction fault status register */
	mrc	p15, 0, r4, c3, c0, 0
	mrc	p15, 0, r5, c5, c0, 0
	mrc	p15, 0, r6, c5, c0, 1
	stmia	r8!, {r4-r6}
	/* Data aux fault status register, instruction aux fault status,
	datat fault address register and instruction fault address register*/
	mrc	p15, 0, r4, c5, c1, 0
	mrc	p15, 0, r5, c5, c1, 1
	mrc	p15, 0, r6, c6, c0, 0
	mrc	p15, 0, r7, c6, c0, 2
	stmia	r8!, {r4-r7}
	/* user r/w thread and process ID, user r/o thread and process ID,
	priv only thread and process ID, cache size selection */
	mrc	p15, 0, r4, c13, c0, 2
	mrc	p15, 0, r5, c13, c0, 3
	mrc	p15, 0, r6, c13, c0, 4
	mrc	p15, 2, r7, c0, c0, 0
	stmia	r8!, {r4-r7}
	/* Data TLB lockdown, instruction TLB lockdown registers */
	mrc	p15, 0, r5, c10, c0, 0
	mrc	p15, 0, r6, c10, c0, 1
	stmia	r8!, {r5-r6}
	/* Secure or non secure vector base address, FCSE PID, Context PID*/
	mrc	p15, 0, r4, c12, c0, 0
	mrc	p15, 0, r5, c13, c0, 0
	mrc	p15, 0, r6, c13, c0, 1
	stmia	r8!, {r4-r6}
	/* Primary remap, normal remap registers */
	mrc	p15, 0, r4, c10, c2, 0
	mrc	p15, 0, r5, c10, c2, 1
	stmia	r8!,{r4-r5}

	/* Store current cpsr*/
	mrs	r2, cpsr
	stmia	r8!, {r2}

	mrc	p15, 0, r4, c1, c0, 0
	/* save control register */
	stmia	r8!, {r4}
clean_caches:
	/* Clean Data or unified cache to POU*/
	/* How to invalidate only L1 cache???? - #FIX_ME# */
	/* mcr	p15, 0, r11, c7, c11, 1 */
	cmp	r9, #1 /* Check whether L2 inval is required or not*/
	bne	skip_l2_inval
clean_l2:
	/* read clidr */
	mrc     p15, 1, r0, c0, c0, 1
	/* extract loc from clidr */
	ands    r3, r0, #0x7000000
	/* left align loc bit field */
	mov     r3, r3, lsr #23
	/* if loc is 0, then no need to clean */
	beq     finished
	/* start clean at cache level 0 */
	mov     r10, #0
loop1:
	/* work out 3x current cache level */
	add     r2, r10, r10, lsr #1
	/* extract cache type bits from clidr*/
	mov     r1, r0, lsr r2
	/* mask of the bits for current cache only */
	and     r1, r1, #7
	/* see what cache we have at this level */
	cmp     r1, #2
	/* skip if no cache, or just i-cache */
	blt     skip
	/* select current cache level in cssr */
	mcr     p15, 2, r10, c0, c0, 0
	/* isb to sych the new cssr&csidr */
	isb
	/* read the new csidr */
	mrc     p15, 1, r1, c0, c0, 0
	/* extract the length of the cache lines */
	and     r2, r1, #7
	/* add 4 (line length offset) */
	add     r2, r2, #4
	ldr     r4, assoc_mask
	/* find maximum number on the way size */
	ands    r4, r4, r1, lsr #3
	/* find bit position of way size increment */
	clz     r5, r4
	ldr     r7, numset_mask
	/* extract max number of the index size*/
	ands    r7, r7, r1, lsr #13
loop2:
	mov     r9, r4
	/* create working copy of max way size*/
loop3:
	/* factor way and cache number into r11 */
	orr     r11, r10, r9, lsl r5
	/* factor index number into r11 */
	orr     r11, r11, r7, lsl r2
	/*clean & invalidate by set/way */
	mcr     p15, 0, r11, c7, c10, 2
	/* decrement the way*/
	subs    r9, r9, #1
	bge     loop3
	/*decrement the index */
	subs    r7, r7, #1
	bge     loop2
skip:
	add     r10, r10, #2
	/* increment cache number */
	cmp     r3, r10
	bgt     loop1
finished:
	/*swith back to cache level 0 */
	mov     r10, #0
	/* select current cache level in cssr */
	mcr     p15, 2, r10, c0, c0, 0
	isb
skip_l2_inval:
	/* Data memory barrier and Data sync barrier */
	mov     r1, #0
	mcr     p15, 0, r1, c7, c10, 4
	mcr     p15, 0, r1, c7, c10, 5

	wfi                             @ wait for interrupt
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	bl i_dll_wait
	/* restore regs and return */
	ldmfd   sp!, {r0-r12, pc}

i_dll_wait:
	ldr     r4, clk_stabilize_delay

i_dll_delay:
	subs    r4, r4, #0x1
	bne     i_dll_delay
	ldr     r4, sdrc_power
	ldr     r5, [r4]
	bic     r5, r5, #0x40
	str     r5, [r4]
	bx	lr
pm_prepwstst_core:
	.word	PM_PREPWSTST_CORE_V
pm_prepwstst_mpu:
	.word	PM_PREPWSTST_MPU_V
pm_pwstctrl_mpu:
	.word	PM_PWSTCTRL_MPU_P
scratchpad_base:
	.word	SCRATCHPAD_BASE_P
sdrc_power:
	.word SDRC_POWER_V
context_mem:
	.word	0x803E3E14
clk_stabilize_delay:
	.word 0x000001FF
assoc_mask:
	.word	0x3ff
numset_mask:
	.word	0x7fff
ttbrbit_mask:
	.word	0xFFFFC000
table_index_mask:
	.word	0xFFF00000
table_entry:
	.word	0x00000C02
cache_pred_disable_mask:
	.word	0xFFFFE7FB
ENTRY(omap34xx_cpu_suspend_sz)
	.word	. - omap34xx_cpu_suspend
