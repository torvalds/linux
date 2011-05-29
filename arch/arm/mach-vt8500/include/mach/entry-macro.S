/*
 * arch/arm/mach-vt8500/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for VIA VT8500
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

	.macro	disable_fiq
	.endm

	.macro  get_irqnr_preamble, base, tmp
	@ physical 0xd8140000 is virtual 0xf8140000
	mov	\base, #0xf8000000
	orr	\base, \base, #0x00140000
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
	.endm

	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
	ldr	\irqnr, [\base]
	cmp	\irqnr, #63 @ may be false positive, check interrupt status
	bne	1001f
	ldr	\irqstat, [\base, #0x84]
	ands	\irqstat, #0x80000000
	moveq	\irqnr, #0
1001:
	.endm

