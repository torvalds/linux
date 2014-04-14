/*
 * Coherency fabric: low level functions
 *
 * Copyright (C) 2012 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * This file implements the assembly function to add a CPU to the
 * coherency fabric. This function is called by each of the secondary
 * CPUs during their early boot in an SMP kernel, this why this
 * function have to callable from assembly. It can also be called by a
 * primary CPU from C code during its boot.
 */

#include <linux/linkage.h>
#define ARMADA_XP_CFB_CTL_REG_OFFSET 0x0
#define ARMADA_XP_CFB_CFG_REG_OFFSET 0x4

#include <asm/assembler.h>
#include <asm/cp15.h>

	.text
/* Returns with the coherency address in r1 (r0 is untouched)*/
ENTRY(ll_get_coherency_base)
	mrc	p15, 0, r1, c1, c0, 0
	tst	r1, #CR_M @ Check MMU bit enabled
	bne	1f

	/* use physical address of the coherency register */
	adr	r1, 3f
	ldr	r3, [r1]
	ldr	r1, [r1, r3]
	b	2f
1:
	/* use virtual address of the coherency register */
	ldr	r1, =coherency_base
	ldr	r1, [r1]
2:
	mov	pc, lr
ENDPROC(ll_get_coherency_base)

/* Returns with the CPU ID in r3 (r0 is untouched)*/
ENTRY(ll_get_cpuid)
	mrc	15, 0, r3, cr0, cr0, 5
	and	r3, r3, #15
	mov	r2, #(1 << 24)
	lsl	r3, r2, r3
ARM_BE8(rev	r1, r1)
	mov	pc, lr
ENDPROC(ll_get_cpuid)

/* ll_add_cpu_to_smp_group, ll_enable_coherency and
 * ll_disable_coherency use strex/ldrex whereas MMU can be off. The
 * Armada XP SoC has an exclusive monitor that can track transactions
 * to Device and/or SO and as such also when MMU is disabled the
 * exclusive transactions will be functional
 */

ENTRY(ll_add_cpu_to_smp_group)
	/*
	 * r0 being untouched in ll_get_coherency_base and
	 * ll_get_cpuid, we can use it to save lr modifing it with the
	 * following bl
	 */
	mov r0, lr
	bl	ll_get_coherency_base
	bl	ll_get_cpuid
	mov lr, r0
	add	r0, r1, #ARMADA_XP_CFB_CFG_REG_OFFSET
1:
	ldrex	r2, [r0]
	orr	r2, r2, r3
	strex	r1, r2, [r0]
	cmp	r1, #0
	bne	1b
	mov	pc, lr
ENDPROC(ll_add_cpu_to_smp_group)

ENTRY(ll_enable_coherency)
	/*
	 * r0 being untouched in ll_get_coherency_base and
	 * ll_get_cpuid, we can use it to save lr modifing it with the
	 * following bl
	 */
	mov r0, lr
	bl	ll_get_coherency_base
	bl	ll_get_cpuid
	mov lr, r0
	add	r0, r1, #ARMADA_XP_CFB_CTL_REG_OFFSET
1:
	ldrex	r2, [r0]
	orr	r2, r2, r3
	strex	r1, r2, [r0]
	cmp	r1, #0
	bne	1b
	dsb
	mov	r0, #0
	mov	pc, lr
ENDPROC(ll_enable_coherency)

ENTRY(ll_disable_coherency)
	/*
	 * r0 being untouched in ll_get_coherency_base and
	 * ll_get_cpuid, we can use it to save lr modifing it with the
	 * following bl
	 */
	mov r0, lr
	bl	ll_get_coherency_base
	bl	ll_get_cpuid
	mov lr, r0
	add	r0, r1, #ARMADA_XP_CFB_CTL_REG_OFFSET
1:
	ldrex	r2, [r0]
	bic	r2, r2, r3
	strex	r1, r2, [r0]
	cmp	r1, #0
	bne	1b
	dsb
	mov	pc, lr
ENDPROC(ll_disable_coherency)

	.align 2
3:
	.long	coherency_phys_base - .
