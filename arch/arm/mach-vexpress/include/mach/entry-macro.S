#include <asm/hardware/entry-macro-gic.S>

	.macro	disable_fiq
	.endm

	.macro	get_irqnr_preamble, base, tmp
	ldr	\base, =gic_cpu_base_addr
	ldr	\base, [\base]
	.endm

	.macro	arch_ret_to_user, tmp1, tmp2
	.endm
