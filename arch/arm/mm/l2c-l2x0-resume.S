/* SPDX-License-Identifier: GPL-2.0 */
/*
 * L2C-310 early resume code.  This can be used by platforms to restore
 * the settings of their L2 cache controller before restoring the
 * processor state.
 *
 * This code can only be used to if you are running in the secure world.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/hardware/cache-l2x0.h>

	.text

ENTRY(l2c310_early_resume)
	adr	r0, 1f
	ldr	r2, [r0]
	add	r0, r2, r0

	ldmia	r0, {r1, r2, r3, r4, r5, r6, r7, r8}
	@ r1 = phys address of L2C-310 controller
	@ r2 = aux_ctrl
	@ r3 = tag_latency
	@ r4 = data_latency
	@ r5 = filter_start
	@ r6 = filter_end
	@ r7 = prefetch_ctrl
	@ r8 = pwr_ctrl

	@ Check that the address has been initialised
	teq	r1, #0
	reteq	lr

	@ The prefetch and power control registers are revision dependent
	@ and can be written whether or not the L2 cache is enabled
	ldr	r0, [r1, #L2X0_CACHE_ID]
	and	r0, r0, #L2X0_CACHE_ID_RTL_MASK
	cmp	r0, #L310_CACHE_ID_RTL_R2P0
	strcs	r7, [r1, #L310_PREFETCH_CTRL]
	cmp	r0, #L310_CACHE_ID_RTL_R3P0
	strcs	r8, [r1, #L310_POWER_CTRL]

	@ Don't setup the L2 cache if it is already enabled
	ldr	r0, [r1, #L2X0_CTRL]
	tst	r0, #L2X0_CTRL_EN
	retne	lr

	str	r3, [r1, #L310_TAG_LATENCY_CTRL]
	str	r4, [r1, #L310_DATA_LATENCY_CTRL]
	str	r6, [r1, #L310_ADDR_FILTER_END]
	str	r5, [r1, #L310_ADDR_FILTER_START]

	str	r2, [r1, #L2X0_AUX_CTRL]
	mov	r9, #L2X0_CTRL_EN
	str	r9, [r1, #L2X0_CTRL]
	ret	lr
ENDPROC(l2c310_early_resume)

	.align
1:	.long	l2x0_saved_regs - .
