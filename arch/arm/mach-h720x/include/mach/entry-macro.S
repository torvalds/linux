/*
 * arch/arm/mach-h720x/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for Hynix HMS720x based platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

		.macro  disable_fiq
		.endm

		.macro  get_irqnr_preamble, base, tmp
		.endm

		.macro  arch_ret_to_user, tmp1, tmp2
		.endm

		.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
#if defined (CONFIG_CPU_H7201) || defined (CONFIG_CPU_H7202)
		@ we could use the id register on H7202, but this is not
		@ properly updated when we come back from asm_do_irq
		@ without a previous return from interrupt
		@ (see loops below in irq_svc, irq_usr)
		@ We see unmasked pending ints only, as the masked pending ints
		@ are not visible here

		mov     \base, #0xf0000000	       @ base register
		orr     \base, \base, #0x24000	       @ irqbase
		ldr     \irqstat, [\base, #0x04]        @ get interrupt status
#if defined (CONFIG_CPU_H7201)
		ldr	\tmp, =0x001fffff
#else
		mvn     \tmp, #0xc0000000
#endif
		and     \irqstat, \irqstat, \tmp        @ mask out unused ints
		mov     \irqnr, #0

		mov     \tmp, #0xff00
		orr     \tmp, \tmp, #0xff
		tst     \irqstat, \tmp
		addeq   \irqnr, \irqnr, #16
		moveq   \irqstat, \irqstat, lsr #16
		tst     \irqstat, #255
		addeq   \irqnr, \irqnr, #8
		moveq   \irqstat, \irqstat, lsr #8
		tst     \irqstat, #15
		addeq   \irqnr, \irqnr, #4
		moveq   \irqstat, \irqstat, lsr #4
		tst     \irqstat, #3
		addeq   \irqnr, \irqnr, #2
		moveq   \irqstat, \irqstat, lsr #2
		tst     \irqstat, #1
		addeq   \irqnr, \irqnr, #1
		moveq   \irqstat, \irqstat, lsr #1
		tst     \irqstat, #1		       @ bit 0 should be set
		.endm

#else
#error hynix processor selection missmatch
#endif

